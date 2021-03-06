/*
 * OpenslSink.h
 *
 * Created on: 2016年5月15日
 *     Author: loushuai
 */

#ifndef JNI_MEDIASINK_AUDIOSINK_OPENSL_OPENSLSINK_H_
#define JNI_MEDIASINK_AUDIOSINK_OPENSL_OPENSLSINK_H_

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <memory>
#include "audiosink.hpp"

namespace whitebean
{

class OpenslSink : public AudioSink
{
public:
	OpenslSink();
	virtual ~OpenslSink();

	int open( uint32_t sampleRate, int channelCount,
            pcm_format_t format=PCM_FORMAT_FIXED_8,
            AudioCallback cb = NULL,
			void *cookie = NULL);

	int start();

	void stop();

private:
	/**
	 *  @brief 创建opensl引擎
	 */
	int CreateEngine();

	/**
	 *  @brief 创建audio player
	 */
	int createBufferQueueAudioPlayer(uint32_t sampleRate,
									 int channelCount,
									 pcm_format_t format);

	/**
	  * @brief  音频播放回调函数
	  * @author  
	  * @param[in]	bq BufferQueue对象接口
	  * @param[in] context 回调所需的数据，PCM数据
	  * @return void
	*/
	static void AudioPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);

	/*
	 * @brief convert digital sample rate to opensl sample rate
	 */
	SLuint32 openslSampleRate(int32_t sr);

	/*
	 * @brief return opensl pcm format
	 */
	SLuint16 openslPcmFormat(pcm_format_t format);

  	/**
	  * @brief  音频播放引擎对象
	*/
	SLObjectItf mEngineObject;
	/**
	  * @brief  音频播放引擎接口
	*/
	SLEngineItf mEngine;
	/**
	  * @brief  音频输出混合器对象
	*/
	SLObjectItf mOutputMixObject;
	/**
	  * @brief  音频播放对象
	*/
	SLObjectItf mPlayerObject;
	/**
	  * @brief  音频播放接口
	*/
	SLPlayItf mPlayerPlay;
	/**
	  * @brief  BufferQueue对象接口
	*/
	SLAndroidSimpleBufferQueueItf mPlayerBufferQueue;
	/*
	 * @brief 音量接口
	 */
	SLVolumeItf mPlayerVolume;

	/*
	 *  @brief 数据填充回调
	 */
	AudioCallback mCallBack;

	/*
	 * @brief pcm buffer
	 */
	std::unique_ptr<uint8_t[]> mBuffer;

	void *mCookie;
};
  
}

#endif /*JNI_MEDIASINK_AUDIOSINK_OPENSL_OPENSLSINK_H_*/
