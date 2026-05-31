#pragma once

#include "pch.h"
#include <thread>
#include <queue>
#include "Utilities/AutoResetEvent.h"
#include "Utilities/SimpleLock.h"
#include "Utilities/Video/IVideoRecorder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class FFmpegRecorder final : public IVideoRecorder
{
private:
	std::thread _writerThread;
	
	AVFormatContext* _formatCtx = nullptr;
	AVCodecContext* _videoCodecCtx = nullptr;
	AVCodecContext* _audioCodecCtx = nullptr;
	AVStream* _videoStream = nullptr;
	AVStream* _audioStream = nullptr;
	SwsContext* _swsCtx = nullptr;
	AVFrame* _videoFrame = nullptr;
	AVFrame* _audioFrame = nullptr;
	AVPacket* _packet = nullptr;

	string _outputFile;
	SimpleLock _lock;
	SimpleLock _audioLock;
	AutoResetEvent _waitFrame;

	atomic<bool> _stopFlag;
	atomic<bool> _framePending;

	std::queue<std::vector<int16_t>> _audioQueue;

	bool _recording = false;
	uint8_t* _frameBuffer = nullptr;
	uint32_t _frameBufferLength = 0;
	uint32_t _sampleRate = 0;

	double _fps = 0;
	uint32_t _width = 0;
	uint32_t _height = 0;
	uint32_t _bpp = 0;

	VideoCodec _codec;
	uint32_t _compressionLevel = 0;

	int64_t _videoPts = 0;
	int64_t _audioPts = 0;

	std::vector<int16_t> _audioBuffer;

	bool InitializeFFmpeg();
	void CleanupFFmpeg();
	bool InitializeVideoStream();
	bool InitializeAudioStream();
	void ProcessFrame();
	void EncodeVideoFrame(uint8_t* frameData);
	void EncodeAudioFrame(int16_t* soundBuffer, uint32_t sampleCount);

public:
	FFmpegRecorder(VideoCodec codec, uint32_t compressionLevel);
	virtual ~FFmpegRecorder();

	bool Init(string filename) override;
	bool StartRecording(uint32_t width, uint32_t height, uint32_t bpp, uint32_t audioSampleRate, double fps) override;
	void StopRecording() override;

	bool AddFrame(void* frameBuffer, uint32_t width, uint32_t height, double fps) override;
	bool AddSound(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate) override;

	bool IsRecording() override;
	string GetOutputFile() override;
};
