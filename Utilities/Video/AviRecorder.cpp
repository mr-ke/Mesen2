#include "pch.h"
#include "AviRecorder.h"
#include <cmath>

AviRecorder::AviRecorder(VideoCodec codec, uint32_t compressionLevel)
{
	_recording = false;
	_stopFlag = false;
	_sampleRate = 0;
	_width = 0;
	_height = 0;
	_bpp = 0;
	_fps = 0.0;
	_codec = codec;
	_compressionLevel = compressionLevel;
}

AviRecorder::~AviRecorder()
{
	if(_recording) {
		StopRecording();
	}
}

bool AviRecorder::Init(string filename)
{
	_outputFile = filename;
	return true;
}

bool AviRecorder::StartRecording(uint32_t width, uint32_t height, uint32_t bpp, uint32_t audioSampleRate, double fps)
{
	if(!_recording) {
		_sampleRate = audioSampleRate;
		_width = width;
		_height = height;
		_bpp = bpp;
		_fps = fps;

		_aviWriter.reset(new AviWriter());
		if(!_aviWriter->StartWrite(_outputFile, _codec, width, height, bpp, (uint32_t)(_fps * 1000000), audioSampleRate, _compressionLevel)) {
			_aviWriter.reset();
			return false;
		}

		_stopFlag = false;
		_recording = true;

		_aviWriterThread = std::thread([this]() {
			uint32_t framesProcessed = 0;
			while(true) {
				// Process all available frames without waiting
				bool processedAny = false;
				while(true) {
					FrameData frame;
					{
						auto lock = _lock.AcquireSafe();
						if(!_frameQueue.empty()) {
							frame = std::move(_frameQueue.front());
							_frameQueue.pop();
						}
					}
					
					if(frame.buffer.empty()) {
						break;
					}
					
					if(_aviWriter) {
						_aviWriter->AddFrame(frame.buffer.data());
						framesProcessed++;
						processedAny = true;
					}
				}
				
				// Exit only when stopped AND queue is empty
				if(_stopFlag) {
					auto lock = _lock.AcquireSafe();
					if(_frameQueue.empty()) {
						break;
					}
				}
				
				// Only wait if we didn't process any frames
				if(!processedAny) {
					_waitFrame.Wait(1);
				}
			}
		});
	}
	return true;
}

void AviRecorder::StopRecording()
{
	if(_recording) {
		// First mark as not recording so AddFrame will stop blocking
		_recording = false;
		
		// Signal encoder thread to stop
		_stopFlag = true;
		_waitFrame.Signal();
		
		// Wait for encoder thread to finish processing all frames
		if(_aviWriterThread.joinable()) {
			_aviWriterThread.join();
		}

		// Finalize the AVI file
		if(_aviWriter) {
			_aviWriter->EndWrite();
			_aviWriter.reset();
		}
		
		// Clear any remaining frames
		auto lock = _lock.AcquireSafe();
		std::queue<FrameData> empty;
		std::swap(_frameQueue, empty);
	}
}

bool AviRecorder::AddFrame(void* frameBuffer, uint32_t width, uint32_t height, double fps)
{
	if(!_recording) {
		return false;
	}
	
	// Allow small floating point differences in fps comparison
	if(_width != width || _height != height || std::abs(_fps - fps) > 0.001) {
		return false;
	}
	
	// Wait for queue to have space (blocking to ensure all frames are recorded)
	while(true) {
		{
			auto lock = _lock.AcquireSafe();
			// Check if recording was stopped while waiting
			if(!_recording || _stopFlag) {
				return false;
			}
			if(_frameQueue.size() < MaxQueueSize) {
				// Add new frame to queue
				FrameData frame;
				frame.buffer.resize(_width * _height * _bpp);
				memcpy(frame.buffer.data(), frameBuffer, frame.buffer.size());
				frame.width = width;
				frame.height = height;
				frame.fps = fps;
				
				_frameQueue.push(std::move(frame));
				_waitFrame.Signal();
				
				return true;
			}
		}
		// Queue is full, wait for encoder to process some frames
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

bool AviRecorder::AddSound(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate)
{
	if(_recording) {
		if(_sampleRate != sampleRate) {
			return false;
		} else {
			_aviWriter->AddSound(soundBuffer, sampleCount);
		}
	}
	return true;
}

bool AviRecorder::IsRecording()
{
	return _recording;
}

string AviRecorder::GetOutputFile()
{
	return _outputFile;
}