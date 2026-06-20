#include "pch.h"
#include "Shared/Audio/AudioPlayer.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"

static constexpr double PI = 3.14159265358979323846;

AudioPlayer::AudioPlayer(Emulator* emu)
{
	_emu = emu;

	for(int i = 0; i < N; i++) {
		_hannWindow[i] = 0.5f * (1.0f - cos(2.0f * PI * (float)(i) / (float)(N - 1.0f)));
	}
}

string AudioPlayer::FormatSeconds(uint32_t s)
{
	string seconds = std::to_string(s % 60);
	if(seconds.size() == 1) {
		seconds = "0" + seconds;
	}
	return std::to_string(s / 60) + ":" + seconds;
}

void AudioPlayer::MoveToNextTrack()
{
	if(!_changeTrackPending) {
		_changeTrackPending = true;
		AudioPlayerActionParams params = {};

		AudioPlayerConfig cfg = _emu->GetSettings()->GetAudioPlayerConfig();
		AudioTrackInfo track = _emu->GetAudioTrackInfo();
		if(!cfg.Repeat) {
			if(cfg.Shuffle) {
				std::random_device rd;
				std::mt19937 mt(rd());
				std::uniform_int_distribution<> dist(0, track.TrackCount - 1);
				params.Action = AudioPlayerAction::SelectTrack;
				params.TrackNumber = dist(mt);
			} else {
				params.Action = AudioPlayerAction::NextTrack;
			}
		} else {
			params.Action = AudioPlayerAction::SelectTrack;
			params.TrackNumber = track.TrackNumber - 1;
		}
		_emu->ProcessAudioPlayerAction(params);
	}
}

uint32_t AudioPlayer::GetVolume()
{
	AudioTrackInfo info = _emu->GetAudioTrackInfo();

	if(info.Length > 0) {
		if(info.Position >= info.Length) {
			MoveToNextTrack();
			return 0;
		} else if(info.Position >= info.Length - info.FadeLength) {
			double fadeStart = info.Length - info.FadeLength;
			double ratio = 1.0 - ((info.Position - fadeStart) / info.FadeLength);
			return (uint32_t)(ratio * _emu->GetSettings()->GetAudioPlayerConfig().Volume);
		}
	}
	return _emu->GetSettings()->GetAudioPlayerConfig().Volume;
}

void AudioPlayer::ProcessSamples(int16_t* samples, size_t sampleCount, uint32_t sampleRate)
{
	_sampleRate = sampleRate;
	for(int i = 0; i < sampleCount; i++) {
		_samples.push_back((samples[i * 2] + samples[i * 2 + 1]) / 2);
		if(_samples.size() > N) {
			_samples.pop_front();
		}
	}

	if(_samples.size() >= N) {
		for(int i = 0; i < N; i++) {
			_input[i] = _samples[i] * _hannWindow[i];
		}

		_fft.transform_real(_input, _out);

		_amplitudes.clear();
		for(int i = 0; i < N / 2; i++) {
			std::complex<double> c = _out[i];
			double amp = sqrt(c.real() * c.real() + c.imag() * c.imag());
			_amplitudes.push_back(amp / N);
		}
	}
}

void AudioPlayer::CheckSilence(uint32_t frameCounter, double fps)
{
	if(_amplitudes.size() < (size_t)(N / 2)) {
		return;
	}

	//Arbitrary ranges to split the graph into (8 equally sized sections on the screen that contain a specific freq range)
	static constexpr double ranges[8][3] {
		{ 20, 150, 0.5 },
		{ 150, 400, 0.5 },
		{ 400, 700, 0.75 },
		{ 700, 1000, 0.75 },
		{ 1000, 2000, 1 },
		{ 2000, 4000, 1 },
		{ 4000, 6000, 1.25 },
		{ 6000, 20000, 1.25 }
	};

	static constexpr int maxVal = 140;
	bool silent = true;

	for(int i = 0; i < 8; i++) {
		for(int j = 0; j < 32; j++) {
			double freqRange = ranges[i][1] - ranges[i][0];
			double startFreq = ranges[i][0] + freqRange * j / 32;
			double endFreq = ranges[i][0] + freqRange * (j + 1) / 32;

			int startIndex = (int)(startFreq / (_sampleRate / N));
			int endIndex = (int)(endFreq / (_sampleRate / N));

			double avgAmp = 0;
			for(int ampIndex = startIndex; ampIndex <= endIndex && ampIndex < (int)_amplitudes.size(); ampIndex++) {
				avgAmp += _amplitudes[ampIndex];
			}
			avgAmp /= (endIndex - startIndex + 1);
			avgAmp *= ranges[i][2];
			avgAmp = std::min<double>(maxVal, avgAmp);

			if(avgAmp >= 1) {
				silent = false;
			}
		}
	}

	if(_prevFrameCounter + 1 != frameCounter || _prevFps != fps || _lastAudioFrame > frameCounter) {
		_prevFrameCounter = frameCounter;
		_lastAudioFrame = frameCounter;
		_prevFps = fps;
	} else {
		_prevFrameCounter = frameCounter;
		if(!silent) {
			_lastAudioFrame = frameCounter;
		} else {
			AudioConfig audioCfg = _emu->GetSettings()->GetAudioConfig();
			double silenceLength = (double)(frameCounter - _lastAudioFrame) / fps;
			if(audioCfg.AudioPlayerAutoDetectSilence && silenceLength >= audioCfg.AudioPlayerSilenceDelay) {
				MoveToNextTrack();
			}
		}
	}
}
