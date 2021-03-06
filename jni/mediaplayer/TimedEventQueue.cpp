/*
 * TimedEventQueue.cpp
 *
 *  Created on: 2016��5��8��
 *      Author: loushuai
 */

#include "log.hpp"
#include "TimedEventQueue.h"
#include <time.h>
#include <limits>
#include <assert.h>

using namespace std;

#define INT64_MIN numeric_limits<int64_t>::min()
#define INT64_MAX numeric_limits<int64_t>::max()

namespace whitebean
{

TimedEventQueue::TimedEventQueue()
: mNextEventID(1)
, mRunning(false)
, mStopped(false)
{
	LOGD("TimedEventQueue");
}

TimedEventQueue::~TimedEventQueue()
{
	LOGD("~TimedEventQueue");
	stop();
}

void TimedEventQueue::start()
{
    if (mRunning) {
        return;
    }

    mStopped = false;

    mThread = thread(TimedEventQueue::ThreadWrapper, this);

    mRunning = true;
}

void TimedEventQueue::stop(bool flush)
{
    if (!mRunning) {
        return;
    }

    if (flush) {
        postEventToBack(shared_ptr<Event> (new StopEvent));
    } else {
        postTimedEvent(shared_ptr<Event> (new StopEvent), INT64_MIN);
    }

    mThread.join();

    mQueue.clear();

    mRunning = false;
}

TimedEventQueue::event_id TimedEventQueue::postEvent(const shared_ptr<Event> &event)
{
    // Reserve an earlier timeslot an INT64_MIN to be able to post
    // the StopEvent to the absolute head of the queue.
    return postTimedEvent(event, INT64_MIN + 1);
}

TimedEventQueue::event_id TimedEventQueue::postEventToBack(
        const shared_ptr<Event> &event)
{
	return postTimedEvent(event, INT64_MAX);
}

TimedEventQueue::event_id TimedEventQueue::postEventWithDelay(
        const shared_ptr<Event> &event, int64_t delay_us) {
    assert (delay_us >= 0);
    return postTimedEvent(event, GetNowUs() + delay_us);
}

TimedEventQueue::event_id TimedEventQueue::postTimedEvent(
        const shared_ptr<Event> &event, int64_t realtime_us)
{
	lock_guard<mutex> lock(mLock);
	event->setEventID(mNextEventID++);

	list<QueueItem>::iterator it = mQueue.begin();
    while (it != mQueue.end() && realtime_us >= (*it).realtime_us) {
        ++it;
    }

    QueueItem item;
    item.event = event;
    item.realtime_us = realtime_us;

    if (it == mQueue.begin()) {
        mQueueHeadChangedCondition.notify_one();
    }

    mQueue.insert(it, item);

    mQueueNotEmptyCondition.notify_one();

    return event->eventID();
}

static bool MatchesEventID(
        void *cookie, const shared_ptr<TimedEventQueue::Event> &event) {
    TimedEventQueue::event_id *id =
        static_cast<TimedEventQueue::event_id *>(cookie);

    if (event->eventID() != *id) {
        return false;
    }

    *id = 0;

    return true;
}

bool TimedEventQueue::cancelEvent(event_id id) {
    if (id == 0) {
        return false;
    }

    cancelEvents(&MatchesEventID, &id, true /* stopAfterFirstMatch */);

    // if MatchesEventID found a match, it will have set id to 0
    // (which is not a valid event_id).

    return id == 0;
}

void TimedEventQueue::cancelEvents(
        bool (*predicate)(void *cookie, const shared_ptr<Event> &event),
        void *cookie,
        bool stopAfterFirstMatch) {
	lock_guard<mutex> lock(mLock);

    auto it = mQueue.begin();
    while (it != mQueue.end()) {
        if (!(*predicate)(cookie, (*it).event)) {
            ++it;
            continue;
        }

        if (it == mQueue.begin()) {
            mQueueHeadChangedCondition.notify_one();
        }

        (*it).event->setEventID(0);
        it = mQueue.erase(it);
        if (stopAfterFirstMatch) {
            return;
        }
    }
}

// static
void *TimedEventQueue::ThreadWrapper(void *me)
{
    static_cast<TimedEventQueue *>(me)->threadEntry();

    return NULL;
}

void TimedEventQueue::threadEntry()
{
	LOGD("Loop thread enter");

	for (;;) {
		int64_t now_us = 0;
		shared_ptr<Event> event;

		{
			unique_lock<mutex> lock(mLock);

			if (mStopped) {
				break;
			}

            while (mQueue.empty()) {
                mQueueNotEmptyCondition.wait(lock);
            }

			event_id eventID = 0;

			for (;;) {
                if (mQueue.empty()) {
                    // The only event in the queue could have been cancelled
                    // while we were waiting for its scheduled time.
                    break;
                }

                list<QueueItem>::iterator it = mQueue.begin();
                eventID = (*it).event->eventID();

                now_us = GetNowUs();
                int64_t when_us = (*it).realtime_us;

                int64_t delay_us;
                if (when_us < 0 || when_us == INT64_MAX) {
                    delay_us = 0;
                } else {
                    delay_us = when_us - now_us;
                }

                if (delay_us <= 0) {
                    break;
                }

                static int64_t kMaxTimeoutUs = 10000000ll;  // 10 secs
                bool timeoutCapped = false;
                if (delay_us > kMaxTimeoutUs) {
                    LOGD("delay_us exceeds max timeout: %lld us", delay_us);

                    // We'll never block for more than 10 secs, instead
                    // we will split up the full timeout into chunks of
                    // 10 secs at a time. This will also avoid overflow
                    // when converting from us to ns.
                    delay_us = kMaxTimeoutUs;
                    timeoutCapped = true;
                }

                auto err = mQueueHeadChangedCondition.wait_for(lock, std::chrono::microseconds(delay_us));
                if (!timeoutCapped && err == std::cv_status::timeout) {
                	now_us = GetNowUs();
                	break;
                }
			}

            // The event w/ this id may have been cancelled while we're
            // waiting for its trigger-time, in that case
            // removeEventFromQueue_l will return NULL.
            // Otherwise, the QueueItem will be removed
            // from the queue and the referenced event returned.
			event = removeEventFromQueue_l(eventID);
		}

		if (event) {
            // Fire event with the lock NOT held.
            event->fire(this, now_us);
		}
	}

	LOGD("Loop thread exit");
}

shared_ptr<TimedEventQueue::Event> TimedEventQueue::removeEventFromQueue_l(event_id id)
{
	for (auto it = mQueue.begin(); it != mQueue.end(); ++it) {
		if ((*it).event->eventID() == id) {
			shared_ptr<Event> event = (*it).event;
			event->setEventID(0);
			mQueue.erase(it);
			return event;
		}
	}

	LOGD("Event %d was not found in the queue, already cancelled?", id);

	return shared_ptr<Event>();
}

inline int64_t TimedEventQueue::GetNowUs()
{
	struct timeval t;
    t.tv_sec = t.tv_usec = 0;
    gettimeofday(&t, NULL);
    return int64_t(t.tv_sec)*1000000LL + int64_t(t.tv_usec);
}

}

