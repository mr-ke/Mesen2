#include "pch.h"
#include "FFmpegRecorder.h"

FFmpegRecorder::FFmpegRecorder(VideoCodec codec, uint32_t compressionLevel)
{
	_recording = false;
	_stopFlag = false;
	_framePending = false;
	_frameBuffer = nullptr;
	_frameBufferLength = 0;
	_sampleRate = 0;
	_codec = codec;
	_compressionLevel = compressionLevel;
	_videoPts = 0;
	_audioPts = 0;
}

FFmpegRecorder::~FFmpegRecorder()
{
	if(_recording) {
		StopRecording();
	}

	if(_frameBuffer) {
		delete[] _frameBuffer;
		_frameBuffer = nullptr;
	}

	CleanupFFmpeg();
}

bool FFmpegRecorder::Init(string filename)
{
	_outputFile = filename;
	return true;
}

bool FFmpegRecorder::InitializeFFmpeg()
{
	const char* format = nullptr;
	
	if(_codec == VideoCodec::ZMBV) {
		format = "avi";
	}

	int ret = avformat_alloc_output_context2(&_formatCtx, nullptr, format, _outputFile.c_str());
	if(ret < 0 || !_formatCtx) {
		return false;
	}

	return true;
}

bool FFmpegRecorder::InitializeVideoStream()
{
	const AVCodec* videoCodec = nullptr;
	AVCodecID codecId = AV_CODEC_ID_NONE;

	switch(_codec) {
		case VideoCodec::ZMBV:
			codecId = AV_CODEC_ID_ZMBV;
			break;
		case VideoCodec::None:
		default:
			codecId = AV_CODEC_ID_RAWVIDEO;
			break;
	}

	videoCodec = avcodec_find_encoder(codecId);
	if(!videoCodec) {
		return false;
	}

	_videoStream = avformat_new_stream(_formatCtx, nullptr);
	if(!_videoStream) {
		return false;
	}

	_videoCodecCtx = avcodec_alloc_context3(videoCodec);
	if(!_videoCodecCtx) {
		return false;
	}

	_videoCodecCtx->codec_id = codecId;
	_videoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	_videoCodecCtx->width = _width;
	_videoCodecCtx->height = _height;
	_videoCodecCtx->time_base = {1, (int)_fps};
	_videoCodecCtx->framerate = {(int)_fps, 1};
	
	if(_codec == VideoCodec::ZMBV) {
		_videoCodecCtx->pix_fmt = AV_PIX_FMT_BGR0;
		_videoCodecCtx->compression_level = _compressionLevel;
		_videoCodecCtx->gop_size = 120;
		_videoCodecCtx->max_b_frames = 0;
	} else {
		_videoCodecCtx->pix_fmt = AV_PIX_FMT_BGR24;
	}

	if(_formatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
		_videoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	int ret = avcodec_open2(_videoCodecCtx, videoCodec, nullptr);
	if(ret < 0) {
		return false;
	}

	ret = avcodec_parameters_from_context(_videoStream->codecpar, _videoCodecCtx);
	if(ret < 0) {
		return false;
	}

	_videoStream->time_base = _videoCodecCtx->time_base;

	_videoFrame = av_frame_alloc();
	if(!_videoFrame) {
		return false;
	}

	_videoFrame->format = _videoCodecCtx->pix_fmt;
	_videoFrame->width = _width;
	_videoFrame->height = _height;

	ret = av_frame_get_buffer(_videoFrame, 0);
	if(ret < 0) {
		return false;
	}

	AVPixelFormat srcPixFmt = AV_PIX_FMT_BGR24;
	if(_bpp == 4) {
		srcPixFmt = AV_PIX_FMT_BGRA;
	} else if(_bpp == 2) {
		srcPixFmt = AV_PIX_FMT_RGB565;
	}

	_swsCtx = sws_getContext(
		_width, _height, srcPixFmt,
		_width, _height, _videoCodecCtx->pix_fmt,
		SWS_BILINEAR, nullptr, nullptr, nullptr
	);

	if(!_swsCtx) {
		return false;
	}

	_packet = av_packet_alloc();
	if(!_packet) {
		return false;
	}

	return true;
}

bool FFmpegRecorder::InitializeAudioStream()
{
	const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if(!audioCodec) {
		return false;
	}

	_audioStream = avformat_new_stream(_formatCtx, nullptr);
	if(!_audioStream) {
		return false;
	}

	_audioCodecCtx = avcodec_alloc_context3(audioCodec);
	if(!_audioCodecCtx) {
		return false;
	}

	_audioCodecCtx->codec_id = AV_CODEC_ID_AAC;
	_audioCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
	_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
	_audioCodecCtx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	_audioCodecCtx->sample_rate = _sampleRate;
	_audioCodecCtx->time_base = {1, (int)_sampleRate};
	_audioCodecCtx->bit_rate = 96000;
	_audioCodecCtx->compression_level = 1;

	if(_formatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
		_audioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	AVDictionary* opts = nullptr;
	av_dict_set(&opts, "aac_coder", "fast", 0);
	int ret = avcodec_open2(_audioCodecCtx, audioCodec, &opts);
	av_dict_free(&opts);
	if(ret < 0) {
		return false;
	}

	ret = avcodec_parameters_from_context(_audioStream->codecpar, _audioCodecCtx);
	if(ret < 0) {
		return false;
	}

	_audioStream->time_base = _audioCodecCtx->time_base;

	_audioFrame = av_frame_alloc();
	if(!_audioFrame) {
		return false;
	}

	_audioFrame->nb_samples = _audioCodecCtx->frame_size;
	_audioFrame->format = _audioCodecCtx->sample_fmt;
	_audioFrame->ch_layout = _audioCodecCtx->ch_layout;
	_audioFrame->sample_rate = _audioCodecCtx->sample_rate;

	if(_audioFrame->nb_samples > 0) {
		ret = av_frame_get_buffer(_audioFrame, 0);
		if(ret < 0) {
			return false;
		}
	}

	return true;
}

bool FFmpegRecorder::StartRecording(uint32_t width, uint32_t height, uint32_t bpp, uint32_t audioSampleRate, double fps)
{
	if(_recording) {
		return true;
	}

	_sampleRate = audioSampleRate;
	_width = width;
	_height = height;
	_bpp = bpp;
	_fps = fps;
	_frameBufferLength = height * width * bpp;
	_frameBuffer = new uint8_t[_frameBufferLength];

	if(!InitializeFFmpeg()) {
		CleanupFFmpeg();
		return false;
	}

	if(!InitializeVideoStream()) {
		CleanupFFmpeg();
		return false;
	}

	if(_sampleRate > 0) {
		if(!InitializeAudioStream()) {
			CleanupFFmpeg();
			return false;
		}
	}

	if(!(_formatCtx->oformat->flags & AVFMT_NOFILE)) {
		int ret = avio_open(&_formatCtx->pb, _outputFile.c_str(), AVIO_FLAG_WRITE);
		if(ret < 0) {
			CleanupFFmpeg();
			return false;
		}
	}

	int ret = avformat_write_header(_formatCtx, nullptr);
	if(ret < 0) {
		CleanupFFmpeg();
		return false;
	}

	_writerThread = std::thread([=]() {
		while(!_stopFlag) {
			_waitFrame.Wait();
			if(_stopFlag) {
				break;
			}

			auto lock = _lock.AcquireSafe();
			ProcessFrame();
			_framePending = false;
		}
	});

	_recording = true;
	return true;
}

void FFmpegRecorder::ProcessFrame()
{
	if(_frameBuffer && _videoFrame) {
		EncodeVideoFrame(_frameBuffer);
	}

	while(true) {
		std::vector<int16_t> audioData;
		{
			auto lock = _audioLock.AcquireSafe();
			if(_audioQueue.empty()) {
				break;
			}
			audioData = std::move(_audioQueue.front());
			_audioQueue.pop();
		}
		EncodeAudioFrame(audioData.data(), audioData.size() / 2);
	}
}

void FFmpegRecorder::EncodeVideoFrame(uint8_t* frameData)
{
	if(!_videoCodecCtx || !_videoFrame) {
		return;
	}

	int ret = av_frame_make_writable(_videoFrame);
	if(ret < 0) {
		return;
	}

	if(_swsCtx) {
		const uint8_t* srcData[1] = { frameData };
		int srcLinesize[1] = { (int)(_width * _bpp) };
		
		sws_scale(_swsCtx, srcData, srcLinesize, 0, _height,
		          _videoFrame->data, _videoFrame->linesize);
	} else {
		memcpy(_videoFrame->data[0], frameData, _frameBufferLength);
	}

	_videoFrame->pts = _videoPts++;

	ret = avcodec_send_frame(_videoCodecCtx, _videoFrame);
	if(ret < 0) {
		return;
	}

	while(ret >= 0) {
		ret = avcodec_receive_packet(_videoCodecCtx, _packet);
		if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		} else if(ret < 0) {
			return;
		}

		av_packet_rescale_ts(_packet, _videoCodecCtx->time_base, _videoStream->time_base);
		_packet->stream_index = _videoStream->index;

		ret = av_interleaved_write_frame(_formatCtx, _packet);
		if(ret < 0) {
			return;
		}
	}
}

void FFmpegRecorder::EncodeAudioFrame(int16_t* soundBuffer, uint32_t sampleCount)
{
	if(!_audioCodecCtx || !_audioFrame) {
		return;
	}

	_audioBuffer.insert(_audioBuffer.end(), soundBuffer, soundBuffer + sampleCount * 2);

	int frameSize = _audioCodecCtx->frame_size;
	
	while(_audioBuffer.size() >= (size_t)frameSize * 2) {
		int ret = av_frame_make_writable(_audioFrame);
		if(ret < 0) {
			return;
		}

		float* leftChannel = reinterpret_cast<float*>(_audioFrame->data[0]);
		float* rightChannel = reinterpret_cast<float*>(_audioFrame->data[1]);
		
		for(int i = 0; i < frameSize; i++) {
			int16_t leftSample = _audioBuffer[i * 2];
			int16_t rightSample = _audioBuffer[i * 2 + 1];
			leftChannel[i] = leftSample / 32768.0f;
			rightChannel[i] = rightSample / 32768.0f;
		}

		_audioFrame->nb_samples = frameSize;
		_audioFrame->pts = _audioPts;
		_audioPts += frameSize;

		_audioBuffer.erase(_audioBuffer.begin(), _audioBuffer.begin() + frameSize * 2);

		ret = avcodec_send_frame(_audioCodecCtx, _audioFrame);
		if(ret < 0) {
			return;
		}

		while(ret >= 0) {
			ret = avcodec_receive_packet(_audioCodecCtx, _packet);
			if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			} else if(ret < 0) {
				return;
			}

			av_packet_rescale_ts(_packet, _audioCodecCtx->time_base, _audioStream->time_base);
			_packet->stream_index = _audioStream->index;

			ret = av_interleaved_write_frame(_formatCtx, _packet);
			if(ret < 0) {
				return;
			}
		}
	}
}

void FFmpegRecorder::StopRecording()
{
	if(!_recording) {
		return;
	}

	_recording = false;
	_stopFlag = true;
	_waitFrame.Signal();

	if(_writerThread.joinable()) {
		_writerThread.join();
	}

	if(_videoCodecCtx) {
		avcodec_send_frame(_videoCodecCtx, nullptr);
		while(true) {
			int ret = avcodec_receive_packet(_videoCodecCtx, _packet);
			if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			}
			if(ret < 0) {
				break;
			}
			av_packet_rescale_ts(_packet, _videoCodecCtx->time_base, _videoStream->time_base);
			_packet->stream_index = _videoStream->index;
			av_interleaved_write_frame(_formatCtx, _packet);
		}
	}

	if(_audioCodecCtx) {
		avcodec_send_frame(_audioCodecCtx, nullptr);
		while(true) {
			int ret = avcodec_receive_packet(_audioCodecCtx, _packet);
			if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			}
			if(ret < 0) {
				break;
			}
			av_packet_rescale_ts(_packet, _audioCodecCtx->time_base, _audioStream->time_base);
			_packet->stream_index = _audioStream->index;
			av_interleaved_write_frame(_formatCtx, _packet);
		}
	}

	if(_formatCtx) {
		av_write_trailer(_formatCtx);
	}

	CleanupFFmpeg();
}

void FFmpegRecorder::CleanupFFmpeg()
{
	if(_packet) {
		av_packet_free(&_packet);
		_packet = nullptr;
	}

	if(_videoFrame) {
		av_frame_free(&_videoFrame);
		_videoFrame = nullptr;
	}

	if(_audioFrame) {
		av_frame_free(&_audioFrame);
		_audioFrame = nullptr;
	}

	if(_swsCtx) {
		sws_freeContext(_swsCtx);
		_swsCtx = nullptr;
	}

	if(_videoCodecCtx) {
		avcodec_free_context(&_videoCodecCtx);
		_videoCodecCtx = nullptr;
	}

	if(_audioCodecCtx) {
		avcodec_free_context(&_audioCodecCtx);
		_audioCodecCtx = nullptr;
	}

	if(_formatCtx) {
		if(!(_formatCtx->oformat->flags & AVFMT_NOFILE)) {
			avio_closep(&_formatCtx->pb);
		}
		avformat_free_context(_formatCtx);
		_formatCtx = nullptr;
	}

	_videoStream = nullptr;
	_audioStream = nullptr;
	_videoPts = 0;
	_audioPts = 0;
	_audioBuffer.clear();

	while(!_audioQueue.empty()) {
		_audioQueue.pop();
	}
}

bool FFmpegRecorder::AddFrame(void* frameBuffer, uint32_t width, uint32_t height, double fps)
{
	if(_recording) {
		if(_width != width || _height != height || _fps != fps) {
			return false;
		}

		while(_framePending) {
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(1));
		}

		auto lock = _lock.AcquireSafe();
		_framePending = true;
		memcpy(_frameBuffer, frameBuffer, _frameBufferLength);
		_waitFrame.Signal();
	}
	return true;
}

bool FFmpegRecorder::AddSound(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate)
{
	if(_recording && _audioCodecCtx) {
		if(_sampleRate != sampleRate) {
			return false;
		}

		auto lock = _audioLock.AcquireSafe();
		_audioQueue.emplace(soundBuffer, soundBuffer + sampleCount * 2);
	}
	return true;
}

bool FFmpegRecorder::IsRecording()
{
	return _recording;
}

string FFmpegRecorder::GetOutputFile()
{
	return _outputFile;
}
