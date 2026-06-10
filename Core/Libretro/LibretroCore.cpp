#include "pch.h"
#include "LibretroCore.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/Interfaces/IConsole.h"
#include "Core/Shared/BaseControlDevice.h"
#include "Core/Shared/BaseControlManager.h"
#include "Utilities/FolderUtilities.h"
#include <fstream>

#ifdef _WIN32
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

// Static instance for callbacks
LibretroCore* LibretroCore::_instance = nullptr;

LibretroCore::LibretroCore(Emulator* emu, const std::string& corePath)
	: _emu(emu)
	, _corePath(corePath)
{
	memset(&_systemInfo, 0, sizeof(_systemInfo));
	memset(&_avInfo, 0, sizeof(_avInfo));
}

LibretroCore::~LibretroCore()
{
	UnloadCore();
}

bool LibretroCore::LoadCore()
{
	if(_coreLoaded) {
		return true;
	}

	// Check if another instance is already loaded
	// This can happen during Reload Rom when a new NdsConsole is created
	// before the old one is destroyed
	if(_instance && _instance != this && _instance->_coreLoaded) {
		// Another instance is active, force unload it first
		// This is necessary to prevent resource conflicts
		MessageManager::Log("[Libretro] Another core instance is active, unloading it first");
		_instance->UnloadCore();
	}

	// Set instance for callbacks
	_instance = this;


	// Load the library
#ifdef _WIN32
	_libraryHandle = LoadLibraryA(_corePath.c_str());
	if(!_libraryHandle) {
		DWORD error = GetLastError();
		MessageManager::DisplayMessage("Libretro", "CouldNotLoadCore", _corePath);
		return false;
	}
#else
	_libraryHandle = dlopen(_corePath.c_str(), RTLD_NOW);
	if(!_libraryHandle) {
		const char* error = dlerror();
		MessageManager::DisplayMessage("Libretro", "CouldNotLoadCore", error ? error : "unknown error");
		return false;
	}
#endif


	// Load function pointers
	if(!LoadFunctions()) {
		UnloadCore();
		return false;
	}

	// Check API version
	if(retro_api_version) {
		unsigned version = retro_api_version();
		if(version != RETRO_API_VERSION) {
			MessageManager::DisplayMessage("Libretro", "ApiVersionMismatch");
			UnloadCore();
			return false;
		}
	}

	// Set up directories BEFORE setting callbacks and calling retro_init
	// The core may request these directories during initialization
	_systemDirectory = FolderUtilities::GetHomeFolder();
	_saveDirectory = FolderUtilities::GetSaveFolder();
	

	// Set callbacks
	if(retro_set_environment) retro_set_environment(EnvironmentCallback);
	if(retro_set_video_refresh) retro_set_video_refresh(VideoRefreshCallback);
	if(retro_set_audio_sample) retro_set_audio_sample(AudioSampleCallback);
	if(retro_set_audio_sample_batch) retro_set_audio_sample_batch(AudioSampleBatchCallback);
	if(retro_set_input_poll) retro_set_input_poll(InputPollCallback);
	if(retro_set_input_state) retro_set_input_state(InputStateCallback);

	// Initialize core
	if(retro_init) {
		retro_init();
	}

	// Get system info
	if(retro_get_system_info) {
		retro_get_system_info(&_systemInfo);
	}

	_coreLoaded = true;
	return true;
}

void LibretroCore::UnloadCore()
{
	if(!_coreLoaded) {
		return;
	}


	// Unload game first
	if(_gameLoaded) {
		UnloadGame();
	}

	// Deinitialize core
	if(retro_deinit) {
		retro_deinit();
	}

	// Unload library
#ifdef _WIN32
	if(_libraryHandle) {
		FreeLibrary((HMODULE)_libraryHandle);
	}
#else
	if(_libraryHandle) {
		dlclose(_libraryHandle);
	}
#endif

	_libraryHandle = nullptr;
	_coreLoaded = false;
	_instance = nullptr;

	// Clear function pointers
	retro_init = nullptr;
	retro_deinit = nullptr;
	retro_api_version = nullptr;
	retro_get_system_info = nullptr;
	retro_get_system_av_info = nullptr;
	retro_set_environment = nullptr;
	retro_set_video_refresh = nullptr;
	retro_set_audio_sample = nullptr;
	retro_set_audio_sample_batch = nullptr;
	retro_set_input_poll = nullptr;
	retro_set_input_state = nullptr;
	retro_set_controller_port_device = nullptr;
	retro_reset = nullptr;
	retro_run = nullptr;
	retro_load_game = nullptr;
	retro_unload_game = nullptr;
	retro_serialize_size = nullptr;
	retro_serialize = nullptr;
	retro_unserialize = nullptr;
	retro_cheat_reset = nullptr;
	retro_cheat_set = nullptr;
	retro_get_region = nullptr;
	retro_get_memory_data = nullptr;
	retro_get_memory_size = nullptr;
}

bool LibretroCore::LoadFunctions()
{
#ifdef _WIN32
	#define LOAD_FUNC(name) \
		name = (name##_t)GetProcAddress((HMODULE)_libraryHandle, #name); \
		if(!name) { \
			MessageManager::DisplayMessage("Libretro", "MissingFunction", #name); \
			return false; \
		}
#else
	#define LOAD_FUNC(name) \
		name = (name##_t)dlsym(_libraryHandle, #name); \
		if(!name) { \
			MessageManager::DisplayMessage("Libretro", "MissingFunction", dlerror()); \
			return false; \
		}
#endif

	// Required functions
	LOAD_FUNC(retro_init);
	LOAD_FUNC(retro_deinit);
	LOAD_FUNC(retro_api_version);
	LOAD_FUNC(retro_get_system_info);
	LOAD_FUNC(retro_get_system_av_info);
	LOAD_FUNC(retro_set_environment);
	LOAD_FUNC(retro_set_video_refresh);
	LOAD_FUNC(retro_set_audio_sample);
	LOAD_FUNC(retro_set_audio_sample_batch);
	LOAD_FUNC(retro_set_input_poll);
	LOAD_FUNC(retro_set_input_state);
	LOAD_FUNC(retro_reset);
	LOAD_FUNC(retro_run);
	LOAD_FUNC(retro_load_game);
	LOAD_FUNC(retro_unload_game);

#undef LOAD_FUNC

#ifdef _WIN32
	#define LOAD_OPTIONAL_FUNC(name) name = (name##_t)GetProcAddress((HMODULE)_libraryHandle, #name);
#else
	#define LOAD_OPTIONAL_FUNC(name) name = (name##_t)dlsym(_libraryHandle, #name);
#endif

	// Optional functions
	LOAD_OPTIONAL_FUNC(retro_set_controller_port_device);
	LOAD_OPTIONAL_FUNC(retro_serialize_size);
	LOAD_OPTIONAL_FUNC(retro_serialize);
	LOAD_OPTIONAL_FUNC(retro_unserialize);
	LOAD_OPTIONAL_FUNC(retro_cheat_reset);
	LOAD_OPTIONAL_FUNC(retro_cheat_set);
	LOAD_OPTIONAL_FUNC(retro_get_region);
	LOAD_OPTIONAL_FUNC(retro_get_memory_data);
	LOAD_OPTIONAL_FUNC(retro_get_memory_size);

#undef LOAD_OPTIONAL_FUNC

	return true;
}

bool LibretroCore::LoadGame(const std::string& romPath, const void* romData, size_t romSize)
{
	if(!_coreLoaded) {
		return false;
	}

	if(_gameLoaded) {
		UnloadGame();
	}


	retro_game_info gameInfo = {};
	gameInfo.path = romPath.c_str();
	gameInfo.data = romData;
	gameInfo.size = romSize;

	bool needFullPath = _systemInfo.need_fullpath;
	
	if(needFullPath) {
		// Core wants the path, not the data
		gameInfo.data = nullptr;
		gameInfo.size = 0;
	} else {
	}

	
	// Log the game info structure
	
	bool result = retro_load_game(&gameInfo);
	
	if(!result) {
		MessageManager::DisplayMessage("Libretro", "CouldNotLoadGame");
		return false;
	}


	// Get AV info
	if(retro_get_system_av_info) {
		retro_get_system_av_info(&_avInfo);
	}

	// Allocate frame buffer (32-bit ARGB)
	uint32_t maxPixels = _avInfo.geometry.max_width * _avInfo.geometry.max_height;
	if(maxPixels == 0) {
		maxPixels = _avInfo.geometry.base_width * _avInfo.geometry.base_height;
	}
	_frameBuffer.resize(maxPixels);
	_frameWidth = _avInfo.geometry.base_width;
	_frameHeight = _avInfo.geometry.base_height;

	_gameLoaded = true;
	return true;
}

void LibretroCore::UnloadGame()
{
	if(!_gameLoaded) {
		return;
	}

	if(retro_unload_game) {
		retro_unload_game();
	}

	_gameLoaded = false;
	_frameBuffer.clear();
	_audioBuffer.clear();
}

void LibretroCore::RunFrame()
{
	if(!_coreLoaded || !_gameLoaded) {
		return;
	}

	if(retro_run) {
		retro_run();
	}
}

void LibretroCore::Reset()
{
	if(!_coreLoaded) {
		return;
	}

	if(retro_reset) {
		retro_reset();
	}
}

int16_t LibretroCore::GetInputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	// Map Mesen's input system to libretro's input
	if(!_emu) {
		return 0;
	}

	// Get the console and control manager
	auto console = _emu->GetConsole();
	if(!console) {
		return 0;
	}

	auto controlManager = console->GetControlManager();
	if(!controlManager) {
		return 0;
	}

	// Find the NDS controller device
	shared_ptr<BaseControlDevice> controller;
	auto devices = controlManager->GetControlDevices();
	for(auto& dev : devices) {
		if(dev && dev->GetControllerType() == ControllerType::NdsController) {
			controller = dev;
			break;
		}
	}

	if(!controller) {
		return 0;
	}

	// Map libretro joypad IDs to NDS controller buttons
	// NdsController::Buttons enum: Up = 0, Down, Left, Right, Start, Select, B, A, Y, X, L, R
	// So: Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7, Y=8, X=9, L=10, R=11
	switch(id) {
		case RETRO_DEVICE_ID_JOYPAD_B:      return controller->IsPressed(6) ? 1 : 0;  // B button (index 6 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_Y:      return controller->IsPressed(8) ? 1 : 0;  // Y button (index 8 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_SELECT: return controller->IsPressed(5) ? 1 : 0;  // Select (index 5 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_START:  return controller->IsPressed(4) ? 1 : 0;  // Start (index 4 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_UP:     return controller->IsPressed(0) ? 1 : 0;  // Up (index 0 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_DOWN:   return controller->IsPressed(1) ? 1 : 0;  // Down (index 1 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_LEFT:   return controller->IsPressed(2) ? 1 : 0;  // Left (index 2 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return controller->IsPressed(3) ? 1 : 0;  // Right (index 3 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_A:      return controller->IsPressed(7) ? 1 : 0;  // A button (index 7 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_X:      return controller->IsPressed(9) ? 1 : 0;  // X button (index 9 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_L:      return controller->IsPressed(10) ? 1 : 0; // L button (index 10 in NdsController)
		case RETRO_DEVICE_ID_JOYPAD_R:      return controller->IsPressed(11) ? 1 : 0; // R button (index 11 in NdsController)
		default: return 0;
	}
}

size_t LibretroCore::GetSerializeSize()
{
	if(!_coreLoaded || !_gameLoaded || !retro_serialize_size) {
		return 0;
	}
	return retro_serialize_size();
}

bool LibretroCore::Serialize(void* data, size_t size)
{
	if(!_coreLoaded || !_gameLoaded || !retro_serialize) {
		return false;
	}
	return retro_serialize(data, size);
}

bool LibretroCore::Unserialize(const void* data, size_t size)
{
	if(!_coreLoaded || !_gameLoaded || !retro_unserialize) {
		return false;
	}
	return retro_unserialize(data, size);
}

void* LibretroCore::GetMemoryData(unsigned id)
{
	if(!_coreLoaded || !_gameLoaded || !retro_get_memory_data) {
		return nullptr;
	}
	return retro_get_memory_data(id);
}

size_t LibretroCore::GetMemorySize(unsigned id)
{
	if(!_coreLoaded || !_gameLoaded || !retro_get_memory_size) {
		return 0;
	}
	return retro_get_memory_size(id);
}

// Static callback implementations

void LibretroCore::RetroLog(int level, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	
	char buffer[2048];
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	
	const char* prefix;
	switch(level) {
		case 0: prefix = "[RETRO_DEBUG] "; break;
		case 1: prefix = "[RETRO_INFO] "; break;
		case 2: prefix = "[RETRO_WARN] "; break;
		case 3: prefix = "[RETRO_ERROR] "; break;
		default: prefix = "[RETRO] "; break;
	}
	
}

bool LibretroCore::EnvironmentCallback(unsigned cmd, void* data)
{
	if(!_instance) return false;


	switch(cmd) {
		case RETRO_ENVIRONMENT_SET_ROTATION: {
			unsigned* rotation = (unsigned*)data;
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
			const char** path = (const char**)data;
			*path = _instance->_systemDirectory.c_str();
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
			const char** path = (const char**)data;
			*path = _instance->_saveDirectory.c_str();
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
			enum retro_pixel_format* format = (enum retro_pixel_format*)data;
			_instance->_pixelFormat = *format;
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
			bool* canDupe = (bool*)data;
			*canDupe = true;
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_OVERSCAN: {
			bool* overscan = (bool*)data;
			*overscan = false;
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_MESSAGE: {
			struct retro_message* msg = (struct retro_message*)data;
			if(msg && msg->msg) {
				MessageManager::DisplayMessage("Libretro", msg->msg);
			}
			return true;
		}
		
		case RETRO_ENVIRONMENT_SHUTDOWN: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_VARIABLE: {
			retro_variable* var = (retro_variable*)data;
			if(!var) return false;
			
			
			// Look for the variable in our stored variables
			for(const auto& v : _instance->_variables) {
				if(v.first == var->key) {
					var->value = v.second.c_str();
					return true;
				}
			}
			
			var->value = nullptr;
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_VARIABLES: {
			const retro_variable* vars = (const retro_variable*)data;
			if(!vars) return false;
			
			_instance->_variables.clear();
			
			// Parse variables and extract defaults
			while(vars->key) {
				std::string key = vars->key;
				std::string value;
				
				// Parse the value string to find default
				// Format: "Description; val1|val2|val3|..."
				// The first value after ';' is the default
				if(vars->value) {
					std::string valStr = vars->value;
					size_t semiPos = valStr.find(';');
					if(semiPos != std::string::npos) {
						// Skip the semicolon and any whitespace
						size_t start = semiPos + 1;
						while(start < valStr.size() && (valStr[start] == ' ' || valStr[start] == '\t')) {
							start++;
						}
						
						// Find the end (pipe character or end of string)
						size_t end = valStr.find('|', start);
						if(end == std::string::npos) {
							end = valStr.size();
						}
						
						value = valStr.substr(start, end - start);
					}
				}
				
				_instance->_variables.push_back({key, value});
				vars++;
			}
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
			bool* updated = (bool*)data;
			*updated = false;
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_GEOMETRY: {
			retro_game_geometry* geo = (retro_game_geometry*)data;
			if(geo) {
				_instance->_avInfo.geometry = *geo;
			}
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
			retro_system_av_info* av = (retro_system_av_info*)data;
			if(av) {
				_instance->_avInfo = *av;
				// Resize frame buffer if needed
				uint32_t maxPixels = av->geometry.max_width * av->geometry.max_height;
				if(maxPixels > _instance->_frameBuffer.size()) {
					_instance->_frameBuffer.resize(maxPixels);
				}
			}
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
			struct retro_log_callback* cb = (struct retro_log_callback*)data;
			if(cb) {
				cb->log = RetroLog;
			}
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_LANGUAGE: {
			unsigned* lang = (unsigned*)data;
			*lang = 0; // RETRO_LANGUAGE_ENGLISH
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES: {
			uint64_t* caps = (uint64_t*)data;
			*caps = (1 << RETRO_DEVICE_JOYPAD) | (1 << RETRO_DEVICE_ANALOG);
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: {
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: {
			const char** path = (const char**)data;
			*path = _instance->_corePath.c_str();
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_HW_RENDER: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_PERF_INTERFACE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_LOCATION_INTERFACE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY: {
			const char** path = (const char**)data;
			*path = _instance->_systemDirectory.c_str();
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_PROC_ADDRESS_CALLBACK: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_USERNAME: {
			const char** username = (const char**)data;
			*username = nullptr;
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS: {
			return false;
		}
		
		// Newer core options API (v1/v2)
		case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: {
			unsigned* version = (unsigned*)data;
			*version = 1; // We support v1 API
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
			// Parse core options similar to SET_VARIABLES
			// For now, just acknowledge
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: {
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
			// Parse core options v2
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
			return true;
		}
		
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK: {
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_LED_INTERFACE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: {
			unsigned* enable = (unsigned*)data;
			*enable = 0x3; // Audio and video enabled
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_MIDI_INTERFACE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_GET_FASTFORWARDING: {
			bool* ff = (bool*)data;
			*ff = false;
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE: {
			float* rate = (float*)data;
			*rate = 60.0f;
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: {
			bool* supported = (bool*)data;
			*supported = false;
			return true;
		}
		
		case RETRO_ENVIRONMENT_GET_THROTTLE_STATE: {
			return false;
		}
		
		case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE: {
			return false;
		}
		
		default:
			return false;
	}
}

void LibretroCore::VideoRefreshCallback(const void* data, unsigned width, unsigned height, size_t pitch)
{
	if(!_instance || !data) return;

	_instance->_frameWidth = width;
	_instance->_frameHeight = height;

	uint32_t* dest = _instance->_frameBuffer.data();
	
	switch(_instance->_pixelFormat) {
		case RETRO_PIXEL_FORMAT_0RGB1555: {
			// Convert 16-bit 0RGB1555 to 32-bit ARGB
			const uint16_t* src = (const uint16_t*)data;
			for(unsigned y = 0; y < height; y++) {
				for(unsigned x = 0; x < width; x++) {
					uint16_t pixel = src[x];
					uint32_t r = (pixel >>  0) & 0x1F;
					uint32_t g = (pixel >>  5) & 0x1F;
					uint32_t b = (pixel >> 10) & 0x1F;
					// Expand 5-bit to 8-bit
					r = (r << 3) | (r >> 2);
					g = (g << 3) | (g >> 2);
					b = (b << 3) | (b >> 2);
					dest[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
				}
				src += pitch / 2;
				dest += width;
			}
			break;
		}
		
		case RETRO_PIXEL_FORMAT_RGB565: {
			// Convert 16-bit RGB565 to 32-bit ARGB
			const uint16_t* src = (const uint16_t*)data;
			for(unsigned y = 0; y < height; y++) {
				for(unsigned x = 0; x < width; x++) {
					uint16_t pixel = src[x];
					uint32_t r = (pixel >>  0) & 0x1F;
					uint32_t g = (pixel >>  5) & 0x3F;
					uint32_t b = (pixel >> 11) & 0x1F;
					// Expand to 8-bit (5-bit uses different expansion than 6-bit)
					r = (r << 3) | (r >> 2);
					g = (g << 2) | (g >> 4);
					b = (b << 3) | (b >> 2);
					dest[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
				}
				src += pitch / 2;
				dest += width;
			}
			break;
		}
		
		case RETRO_PIXEL_FORMAT_XRGB8888: {
			// Just add alpha channel
			const uint32_t* src = (const uint32_t*)data;
			for(unsigned y = 0; y < height; y++) {
				for(unsigned x = 0; x < width; x++) {
					dest[x] = src[x] | 0xFF000000;
				}
				src += pitch / 4;
				dest += width;
			}
			break;
		}
		
		default:
			break;
	}
}

void LibretroCore::AudioSampleCallback(int16_t left, int16_t right)
{
	if(!_instance) return;

	_instance->_audioBuffer.push_back(left);
	_instance->_audioBuffer.push_back(right);
}

size_t LibretroCore::AudioSampleBatchCallback(const int16_t* data, size_t frames)
{
	if(!_instance) return 0;

	for(size_t i = 0; i < frames * 2; i++) {
		_instance->_audioBuffer.push_back(data[i]);
	}

	return frames;
}

void LibretroCore::InputPollCallback()
{
	// Update input state from Mesen's input system
	if(_instance && _instance->_emu) {
		auto console = _instance->_emu->GetConsole();
		if(console) {
			auto controlManager = console->GetControlManager();
			if(controlManager) {
				controlManager->UpdateInputState();
			}
		}
	}
}

int16_t LibretroCore::InputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if(!_instance) return 0;
	return _instance->GetInputState(port, device, index, id);
}
