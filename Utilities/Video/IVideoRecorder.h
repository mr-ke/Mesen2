#pragma once
#include "pch.h"

enum class VideoCodec
{
	None = 0,
	ZMBV = 1,
	FFVHUFF = 2,
	UTVideo = 3,
	H264 = 4,
	VP8 = 5,
	VP9 = 6
};

class IVideoRecorder
{
public:
	virtual ~IVideoRecorder() = default;

	virtual bool Init(string filename) = 0;

	virtual bool StartRecording(uint32_t width, uint32_t height, uint32_t bpp, uint32_t audioSampleRate, double fps) = 0;
	virtual void StopRecording() = 0;

	virtual bool AddFrame(void* frameBuffer, uint32_t width, uint32_t height, double fps) = 0;
	virtual bool AddSound(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate) = 0;

	virtual bool IsRecording() = 0;
	virtual string GetOutputFile() = 0;
};