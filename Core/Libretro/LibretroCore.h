#pragma once
#include "pch.h"
#include "LibretroApi.h"
#include "Core/Shared/Interfaces/IConsole.h"
#include "Core/Shared/RomInfo.h"
#include "Core/Debugger/DebugTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdarg>

class Emulator;
class BaseControlManager;

class LibretroCore
{
public:
	LibretroCore(Emulator* emu, const std::string& corePath);
	~LibretroCore();

	// Load the libretro core library
	bool LoadCore();
	
	// Unload the libretro core library
	void UnloadCore();
	
	// Load a ROM
	bool LoadGame(const std::string& romPath, const void* romData, size_t romSize);
	
	// Unload the current game
	void UnloadGame();
	
	// Run a single frame
	void RunFrame();
	
	// Reset the core
	void Reset();
	
	// Get system information
	const retro_system_info& GetSystemInfo() const { return _systemInfo; }
	const retro_system_av_info& GetSystemAvInfo() const { return _avInfo; }
	
	// Get frame buffer (32-bit ARGB format)
	uint32_t* GetFrameBuffer() { return _frameBuffer.data(); }
	uint32_t GetFrameWidth() const { return _frameWidth; }
	uint32_t GetFrameHeight() const { return _frameHeight; }
	
	// Get audio buffer
	const std::vector<int16_t>& GetAudioBuffer() const { return _audioBuffer; }
	void ClearAudioBuffer() { _audioBuffer.clear(); }
	
	// Input state callback (called by the core)
	int16_t GetInputState(unsigned port, unsigned device, unsigned index, unsigned id);
	
	// Check if core is loaded
	bool IsLoaded() const { return _coreLoaded; }
	
	// Check if game is loaded
	bool IsGameLoaded() const { return _gameLoaded; }
	
	// Get save state size
	size_t GetSerializeSize();
	
	// Save/load state
	bool Serialize(void* data, size_t size);
	bool Unserialize(const void* data, size_t size);
	
	// Memory access
	void* GetMemoryData(unsigned id);
	size_t GetMemorySize(unsigned id);

private:
	// Load function pointers from the library
	bool LoadFunctions();
	
	// Logging callback for libretro
	static void RetroLog(int level, const char* fmt, ...);
	
	// Callback implementations (static, passed to the core)
	static bool EnvironmentCallback(unsigned cmd, void* data);
	static void VideoRefreshCallback(const void* data, unsigned width, unsigned height, size_t pitch);
	static void AudioSampleCallback(int16_t left, int16_t right);
	static size_t AudioSampleBatchCallback(const int16_t* data, size_t frames);
	static void InputPollCallback();
	static int16_t InputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id);

private:
	Emulator* _emu = nullptr;
	std::string _corePath;
	
	// Library handle
	void* _libraryHandle = nullptr;
	
	// Core state
	bool _coreLoaded = false;
	bool _gameLoaded = false;
	
	// Core function pointers (no leading underscore to match type names)
	retro_init_t retro_init = nullptr;
	retro_deinit_t retro_deinit = nullptr;
	retro_api_version_t retro_api_version = nullptr;
	retro_get_system_info_t retro_get_system_info = nullptr;
	retro_get_system_av_info_t retro_get_system_av_info = nullptr;
	retro_set_environment_t retro_set_environment = nullptr;
	retro_set_video_refresh_t retro_set_video_refresh = nullptr;
	retro_set_audio_sample_t retro_set_audio_sample = nullptr;
	retro_set_audio_sample_batch_t retro_set_audio_sample_batch = nullptr;
	retro_set_input_poll_t retro_set_input_poll = nullptr;
	retro_set_input_state_t retro_set_input_state = nullptr;
	retro_set_controller_port_device_t retro_set_controller_port_device = nullptr;
	retro_reset_t retro_reset = nullptr;
	retro_run_t retro_run = nullptr;
	retro_load_game_t retro_load_game = nullptr;
	retro_unload_game_t retro_unload_game = nullptr;
	retro_serialize_size_t retro_serialize_size = nullptr;
	retro_serialize_t retro_serialize = nullptr;
	retro_unserialize_t retro_unserialize = nullptr;
	retro_cheat_reset_t retro_cheat_reset = nullptr;
	retro_cheat_set_t retro_cheat_set = nullptr;
	retro_get_region_t retro_get_region = nullptr;
	retro_get_memory_data_t retro_get_memory_data = nullptr;
	retro_get_memory_size_t retro_get_memory_size = nullptr;
	
	// System info
	retro_system_info _systemInfo = {};
	retro_system_av_info _avInfo = {};
	
	// Video buffer (always 32-bit ARGB format)
	std::vector<uint32_t> _frameBuffer;
	uint32_t _frameWidth = 0;
	uint32_t _frameHeight = 0;
	retro_pixel_format _pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
	
	// Audio buffer
	std::vector<int16_t> _audioBuffer;
	
	// Core variables (options)
	std::vector<std::pair<std::string, std::string>> _variables;
	
	// Current instance for callbacks
	static LibretroCore* _instance;
	
	// Environment variables
	std::string _systemDirectory;
	std::string _saveDirectory;
};
