#pragma once
#include "pch.h"
#include <thread>
#include <queue>
#include "Utilities/AutoResetEvent.h"
#include "Utilities/SimpleLock.h"
#include "Utilities/Video/AviWriter.h"
#include "Utilities/Video/IVideoRecorder.h"

class AviRecorder final : public IVideoRecorder
{
private:
	struct FrameData
	{
		vector<uint8_t> buffer;
		uint32_t width;
		uint32_t height;
		double fps;
	};

	std::thread _aviWriterThread;
	
	unique_ptr<AviWriter> _aviWriter;

	string _outputFile;
	SimpleLock _lock;
	AutoResetEvent _waitFrame;

	atomic<bool> _stopFlag;
	
	// Frame queue for async encoding
	std::queue<FrameData> _frameQueue;
	static constexpr size_t MaxQueueSize = 10; // Increased from 3 to allow more buffering

	atomic<bool> _recording;
	uint32_t _sampleRate;

	double _fps;
	uint32_t _width;
	uint32_t _height;
	uint32_t _bpp;

	VideoCodec _codec;
	uint32_t _compressionLevel;

public:
	AviRecorder(VideoCodec codec, uint32_t compressionLevel);
	virtual ~AviRecorder();

	bool Init(string filename) override;
	bool StartRecording(uint32_t width, uint32_t height, uint32_t bpp, uint32_t audioSampleRate, double fps) override;
	void StopRecording() override;

	bool AddFrame(void* frameBuffer, uint32_t width, uint32_t height, double fps) override;
	bool AddSound(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate) override;

	bool IsRecording() override;
	string GetOutputFile() override;
};