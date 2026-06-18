#include "pch.h"
#include "LibretroCore.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/Interfaces/IConsole.h"
#include "Core/Shared/BaseControlDevice.h"
#include "Core/Shared/BaseControlManager.h"
#include "Core/Shared/KeyManager.h"
#include "Utilities/FolderUtilities.h"
#include <fstream>

#ifdef USE_SDL
	#include <SDL.h>
	#include <SDL_syswm.h>
#endif

#ifdef _WIN32
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

// Static instance for callbacks
LibretroCore* LibretroCore::_instance = nullptr;
bool LibretroCore::_forceOpenGL = false;
SDL_Window* LibretroCore::_sdlWindow = nullptr;

LibretroCore::LibretroCore(Emulator* emu, const std::string& corePath)
	: _emu(emu)
	, _corePath(corePath)
{
	memset(&_systemInfo, 0, sizeof(_systemInfo));
	memset(&_avInfo, 0, sizeof(_avInfo));
	memset(&_hwRenderCallback, 0, sizeof(_hwRenderCallback));
}

bool LibretroCore::ExtensionNeedsOpenGL(const std::string& extension)
{
	// Extensions that require OpenGL hardware rendering
	static const std::vector<std::string> openglExtensions = {
		".cci", ".cia", ".3ds", ".3dsx"  // 3DS
	};
	
	std::string ext = extension;
	// Convert to lowercase
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	
	for(const auto& e : openglExtensions) {
		if(ext == e) {
			return true;
		}
	}
	return false;
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
		_instance->UnloadCore();
	}

	// Set instance for callbacks
	_instance = this;


	// Load the library
#ifdef _WIN32
	_libraryHandle = LoadLibraryA(_corePath.c_str());
	if(!_libraryHandle) {
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

	// DON'T clear instance pointer yet - callbacks during unload may need it
	// We'll clear it at the end after all callbacks are done

	// Unload game first - the core may need OpenGL context to clean up resources
	if(_gameLoaded) {
		UnloadGame();
	}

	// Destroy OpenGL context after game is unloaded
	if(_useHwRender) {
		DestroyOpenGLContext();
	}

	// Deinitialize core
	// Skip retro_deinit if retro_unload_game was not called (e.g., GL context
	// MakeCurrent failed). Calling retro_deinit on a core with loaded game state
	// causes access violations because the core tries to clean up resources that
	// require a valid GL context (which has already been destroyed).
	if(retro_deinit && _gameUnloaded) {
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
	_useHwRender = false;

	// NOW clear the instance pointer - all callbacks are done
	if(_instance == this) {
		_instance = nullptr;
	}

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
	_gameUnloaded = false;
	return true;
}

void LibretroCore::UnloadGame()
{
	if(!_gameLoaded) {
		return;
	}

	// For hardware rendering cores, make GL context current before unloading game
	// The core may need to clean up OpenGL resources during retro_unload_game
#ifdef USE_SDL
	if(_useHwRender) {
		if(!_glContext || !_glWindow) {
			_gameLoaded = false;
			_frameBuffer.clear();
			_audioBuffer.clear();
			return;
		}

		int result = SDL_GL_MakeCurrent(_glWindow, _glContext);
		if(result != 0) {
			_gameLoaded = false;
			_frameBuffer.clear();
			_audioBuffer.clear();
			return;
		}
		
		// Reset OpenGL state to a clean state
		// This helps ensure the core's cleanup code works correctly
		typedef void (*glBindFramebufferPROC)(unsigned int target, unsigned int framebuffer);
		typedef void (*glBindTexturePROC)(unsigned int target, unsigned int texture);
		typedef void (*glBindBufferPROC)(unsigned int target, unsigned int buffer);
		typedef void (*glBindVertexArrayPROC)(unsigned int array);
		typedef void (*glUseProgramPROC)(unsigned int program);
		
		auto glBindFramebuffer = (glBindFramebufferPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
		auto glBindTexture = (glBindTexturePROC)SDL_GL_GetProcAddress("glBindTexture");
		auto glBindBuffer = (glBindBufferPROC)SDL_GL_GetProcAddress("glBindBuffer");
		auto glBindVertexArray = (glBindVertexArrayPROC)SDL_GL_GetProcAddress("glBindVertexArray");
		auto glUseProgram = (glUseProgramPROC)SDL_GL_GetProcAddress("glUseProgram");
		
		if(glBindFramebuffer) glBindFramebuffer(0x8D40, 0); // GL_FRAMEBUFFER
		if(glBindTexture) glBindTexture(0x0DE1, 0); // GL_TEXTURE_2D
		if(glBindBuffer) glBindBuffer(0x8892, 0); // GL_ARRAY_BUFFER
		if(glBindVertexArray) glBindVertexArray(0);
		if(glUseProgram) glUseProgram(0);
	}
#endif

	if(retro_unload_game) {
		retro_unload_game();
		_gameUnloaded = true;
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

#ifdef USE_SDL
	// Initialize OpenGL context on first run if needed
	if(_useHwRender && _hwContextNeedsInit) {
		if(!InitOpenGLContext()) {
			_useHwRender = false;
		}
		_hwContextNeedsInit = false;
	}
	
	// Make GL context current for hardware rendering
	if(_useHwRender && _glContext) {
		SDL_GL_MakeCurrent(_glWindow, _glContext);
	}
#endif

	if(retro_run) {
		retro_run();
	}

#ifdef USE_SDL
	// Release GL context after frame to allow other threads to use it
	// This is important for Reload ROM functionality
	if(_useHwRender && _glContext) {
		SDL_GL_MakeCurrent(nullptr, nullptr);
	}
#endif
}

void LibretroCore::Reset()
{
	if(!_coreLoaded) {
		return;
	}

#ifdef USE_SDL
	// Make GL context current before calling retro_reset for hardware rendering cores
	// The core may need OpenGL access during reset
	if(_useHwRender && _glContext) {
		SDL_GL_MakeCurrent(_glWindow, _glContext);
	}
#endif

	if(retro_reset) {
		retro_reset();
	}

#ifdef USE_SDL
	// Release GL context after reset to allow other threads to use it
	if(_useHwRender && _glContext) {
		SDL_GL_MakeCurrent(nullptr, nullptr);
	}
#endif
}

int16_t LibretroCore::GetInputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	// Handle pointer device (touchscreen)
	if(device == RETRO_DEVICE_POINTER) {
		MousePosition mousePos = KeyManager::GetMousePosition();
		
		// Check if mouse is within the screen (valid coordinates)
		if(mousePos.RelativeX < 0 || mousePos.RelativeY < 0) {
			return 0;  // Mouse is off-screen
		}
		
		switch(id) {
			case RETRO_DEVICE_ID_POINTER_X:
				// Convert from [0.0, 1.0] to [-32767, 32767]
				return (int16_t)(mousePos.RelativeX * 65534.0 - 32767.0);
			
			case RETRO_DEVICE_ID_POINTER_Y:
				// Convert from [0.0, 1.0] to [-32767, 32767]
				return (int16_t)(mousePos.RelativeY * 65534.0 - 32767.0);
			
			case RETRO_DEVICE_ID_POINTER_PRESSED:
				// Return 1 if left mouse button is pressed
				return KeyManager::IsMouseButtonPressed(MouseButton::LeftButton) ? 1 : 0;
			
			default:
				return 0;
		}
	}
	
	// Handle joypad device
	if(device == RETRO_DEVICE_JOYPAD) {
		// Get the console and control manager
		auto console = _emu->GetConsole();
		if(!console) {
			return 0;
		}

		auto controlManager = console->GetControlManager();
		if(!controlManager) {
			return 0;
		}

		// Find the controller device (NDS or 3DS)
		shared_ptr<BaseControlDevice> controller;
		auto devices = controlManager->GetControlDevices();
		for(auto& dev : devices) {
			if(dev && (dev->GetControllerType() == ControllerType::NdsController || 
			           dev->GetControllerType() == ControllerType::ThreeDsController)) {
				controller = dev;
				break;
			}
		}

		if(!controller) {
			return 0;
		}

		// Check if this is a 3DS controller (has more buttons)
		bool isThreeDs = (controller->GetControllerType() == ControllerType::ThreeDsController);

		// Map libretro joypad IDs to controller buttons
		// NdsController::Buttons enum: Up = 0, Down, Left, Right, Start, Select, B, A, Y, X, L, R
		// ThreeDsController::Buttons enum: Up = 0, Down, Left, Right, Start, Select, B, A, Y, X, L, R, ZL, ZR, Home, Power, ...
		// So: Up=0, Down=1, Left=2, Right=3, Start=4, Select=5, B=6, A=7, Y=8, X=9, L=10, R=11
		int16_t result = 0;
		switch(id) {
			case RETRO_DEVICE_ID_JOYPAD_B:      result = controller->IsPressed(6) ? 1 : 0; break;  // B button
			case RETRO_DEVICE_ID_JOYPAD_Y:      result = controller->IsPressed(8) ? 1 : 0; break;  // Y button
			case RETRO_DEVICE_ID_JOYPAD_SELECT: result = controller->IsPressed(5) ? 1 : 0; break;  // Select
			case RETRO_DEVICE_ID_JOYPAD_START:  result = controller->IsPressed(4) ? 1 : 0; break;  // Start
			case RETRO_DEVICE_ID_JOYPAD_UP:     result = controller->IsPressed(0) ? 1 : 0; break;  // Up
			case RETRO_DEVICE_ID_JOYPAD_DOWN:   result = controller->IsPressed(1) ? 1 : 0; break;  // Down
			case RETRO_DEVICE_ID_JOYPAD_LEFT:   result = controller->IsPressed(2) ? 1 : 0; break;  // Left
			case RETRO_DEVICE_ID_JOYPAD_RIGHT:  result = controller->IsPressed(3) ? 1 : 0; break;  // Right
			case RETRO_DEVICE_ID_JOYPAD_A:      result = controller->IsPressed(7) ? 1 : 0; break;  // A button
			case RETRO_DEVICE_ID_JOYPAD_X:      result = controller->IsPressed(9) ? 1 : 0; break;  // X button
			case RETRO_DEVICE_ID_JOYPAD_L:      result = controller->IsPressed(10) ? 1 : 0; break; // L button
			case RETRO_DEVICE_ID_JOYPAD_R:      result = controller->IsPressed(11) ? 1 : 0; break; // R button
			case RETRO_DEVICE_ID_JOYPAD_L2:     result = (isThreeDs && controller->IsPressed(12)) ? 1 : 0; break; // ZL button (3DS only)
			case RETRO_DEVICE_ID_JOYPAD_R2:     result = (isThreeDs && controller->IsPressed(13)) ? 1 : 0; break; // ZR button (3DS only)
			case RETRO_DEVICE_ID_JOYPAD_L3:     result = 0; break; // Not used
			case RETRO_DEVICE_ID_JOYPAD_R3:     result = 0; break; // Not used
			default: result = 0; break;
		}
		
		return result;
	}
	
	return 0;
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

#ifdef USE_SDL
	// Make GL context current before calling retro_serialize for hardware rendering cores
	// The core may need OpenGL access during serialization (e.g., to save texture state)
	if(_useHwRender && _glContext) {
		SDL_GL_MakeCurrent(_glWindow, _glContext);
	}
#endif

	bool result = retro_serialize(data, size);

#ifdef USE_SDL
	// Release GL context after serialization
	if(_useHwRender && _glContext) {
		SDL_GL_MakeCurrent(nullptr, nullptr);
	}
#endif

	return result;
}

bool LibretroCore::Unserialize(const void* data, size_t size)
{
	if(!_coreLoaded || !_gameLoaded || !retro_unserialize) {
		return false;
	}

#ifdef USE_SDL
	// Make GL context current before calling retro_unserialize for hardware rendering cores
	// The core may need OpenGL access during deserialization (e.g., to restore texture state)
	if(_useHwRender && _glContext) {
		SDL_GL_MakeCurrent(_glWindow, _glContext);
	}
#endif

	bool result = retro_unserialize(data, size);

#ifdef USE_SDL
	// Release GL context after deserialization
	if(_useHwRender && _glContext) {
		SDL_GL_MakeCurrent(nullptr, nullptr);
	}
#endif

	return result;
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
	// Logging is disabled - can be re-enabled for debugging
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
			retro_hw_render_callback* hwRender = (retro_hw_render_callback*)data;
			if(!hwRender) return false;
			
			// Check if we support the requested context type
			// We support OpenGL, OpenGL Core, OpenGL ES, and Vulkan
			if(hwRender->context_type != RETRO_HW_CONTEXT_OPENGL &&
			   hwRender->context_type != RETRO_HW_CONTEXT_OPENGL_CORE &&
			   hwRender->context_type != RETRO_HW_CONTEXT_OPENGLES2 &&
			   hwRender->context_type != RETRO_HW_CONTEXT_OPENGLES3 &&
			   hwRender->context_type != RETRO_HW_CONTEXT_VULKAN) {
				return false;
			}
			
			// Store the callback structure
			_instance->_hwRenderCallback = *hwRender;
			
			// Set our callbacks - these will be called by the core when it needs them
			_instance->_hwRenderCallback.get_current_framebuffer = HwGetCurrentFramebuffer;
			_instance->_hwRenderCallback.get_proc_address = HwGetProcAddress;
			
			// Mark that we need hardware rendering (but don't create context yet)
			_instance->_useHwRender = true;
			_instance->_hwContextNeedsInit = true;
			
			return true;
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
	if(!_instance) return;

	_instance->_frameWidth = width;
	_instance->_frameHeight = height;

	// Handle hardware rendering case
	// When HW_RENDER is used, data can be:
	// - NULL (core didn't render anything)
	// - RETRO_HW_FRAME_BUFFER_VALID ((void*)-1) - core rendered to FBO
	// Hardware rendering is indicated by pitch == 0
	if(!data || data == (const void*)-1 || pitch == 0) {
		// Hardware rendering - read pixels from OpenGL framebuffer
#ifdef USE_SDL
		if(_instance->_useHwRender && _instance->_glContext) {
			SDL_GL_MakeCurrent(_instance->_glWindow, _instance->_glContext);
			
			// Resize frame buffer if needed
			size_t requiredSize = width * height;
			if(_instance->_frameBuffer.size() < requiredSize) {
				_instance->_frameBuffer.resize(requiredSize);
			}
			
			// Get OpenGL functions
			typedef void (*glBindFramebufferPROC)(unsigned int target, unsigned int framebuffer);
			typedef void (*glReadPixelsPROC)(int x, int y, int width, int height, unsigned int format, unsigned int type, void* data);
			typedef void (*glFinishPROC)(void);
			typedef int (*glGetIntegervPROC)(unsigned int pname, int* params);
			typedef unsigned int GLenum;
			typedef GLenum (*glGetErrorPROC)(void);
			
			glBindFramebufferPROC glBindFramebuffer = (glBindFramebufferPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
			glReadPixelsPROC glReadPixels = (glReadPixelsPROC)SDL_GL_GetProcAddress("glReadPixels");
			glFinishPROC glFinish = (glFinishPROC)SDL_GL_GetProcAddress("glFinish");
			glGetIntegervPROC glGetIntegerv = (glGetIntegervPROC)SDL_GL_GetProcAddress("glGetIntegerv");
			glGetErrorPROC glGetError = (glGetErrorPROC)SDL_GL_GetProcAddress("glGetError");
			
			if(glReadPixels) {
				// Clear any previous GL errors
				if(glGetError) { while(glGetError() != 0) {} }
				
				// First, try to read from the default framebuffer (0)
				// Some cores render to the default framebuffer instead of the FBO
				int currentFbo = 0;
				if(glGetIntegerv && glBindFramebuffer) {
					glGetIntegerv(0x8CA6, &currentFbo); // GL_FRAMEBUFFER_BINDING
				}
				
				// Ensure all rendering is complete
				if(glFinish) glFinish();
				
				// Read pixels from the currently bound framebuffer
				// GL_BGRA = 0x80E1, GL_UNSIGNED_BYTE = 0x1401
				glReadPixels(0, 0, width, height, 0x80E1, 0x1401, _instance->_frameBuffer.data());
				
				// Check for errors
				GLenum err = glGetError ? glGetError() : 0;
				if(err == 0) {
					// Flip the image vertically (OpenGL origin is bottom-left, we need top-left)
					std::vector<uint32_t> tempRow(width);
					for(unsigned y = 0; y < height / 2; y++) {
						uint32_t* topRow = _instance->_frameBuffer.data() + y * width;
						uint32_t* bottomRow = _instance->_frameBuffer.data() + (height - 1 - y) * width;
						memcpy(tempRow.data(), topRow, width * sizeof(uint32_t));
						memcpy(topRow, bottomRow, width * sizeof(uint32_t));
						memcpy(bottomRow, tempRow.data(), width * sizeof(uint32_t));
					}
				}
			}
		}
#endif
		return;
	}

	// Software rendering path

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

// Hardware rendering implementation
bool LibretroCore::InitOpenGLContext()
{
#ifdef USE_SDL
	// Initialize SDL video subsystem if not already initialized
	if(!SDL_WasInit(SDL_INIT_VIDEO)) {
		if(SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
			return false;
		}
	}
	
	// Set OpenGL attributes based on requested context type
	switch(_hwRenderCallback.context_type) {
		case RETRO_HW_CONTEXT_OPENGL_CORE:
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			if(_hwRenderCallback.version_major > 0) {
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, _hwRenderCallback.version_major);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, _hwRenderCallback.version_minor);
			}
			break;
		case RETRO_HW_CONTEXT_OPENGLES2:
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
			break;
		case RETRO_HW_CONTEXT_OPENGLES3:
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
			break;
		default:
			// Use compatibility profile for RETRO_HW_CONTEXT_OPENGL
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
			break;
	}
	
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, _hwRenderCallback.depth ? 24 : 0);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, _hwRenderCallback.stencil ? 8 : 0);
	
	// Use the SDL window provided by SdlRenderer if available
	if(_sdlWindow) {
		_glWindow = _sdlWindow;
	} else {
		// Create a hidden window for OpenGL context
		_glWindow = SDL_CreateWindow(
			"Libretro GL",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			640, 480,
			SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN
		);
		
		if(!_glWindow) {
			return false;
		}
	}
	
	_glContext = SDL_GL_CreateContext(_glWindow);
	if(!_glContext) {
		if(_glWindow != _sdlWindow) {
			SDL_DestroyWindow(_glWindow);
		}
		_glWindow = nullptr;
		return false;
	}
	
	SDL_GL_MakeCurrent(_glWindow, _glContext);
	
	// Create a framebuffer for the core to render to
	typedef unsigned int GLenum;
	typedef unsigned int GLuint;
	typedef int GLsizei;
	typedef void (*glGenFramebuffersPROC)(GLsizei n, GLuint* framebuffers);
	typedef void (*glBindFramebufferPROC)(GLenum target, GLuint framebuffer);
	typedef void (*glGenRenderbuffersPROC)(GLsizei n, GLuint* renderbuffers);
	typedef void (*glBindRenderbufferPROC)(GLenum target, GLuint renderbuffer);
	typedef void (*glRenderbufferStoragePROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
	typedef void (*glFramebufferRenderbufferPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
	typedef int (*glCheckFramebufferStatusPROC)(GLenum target);
	typedef void (*glGetIntegervPROC)(GLenum pname, int* params);
	
	glGenFramebuffersPROC glGenFramebuffers = (glGenFramebuffersPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
	glBindFramebufferPROC glBindFramebuffer = (glBindFramebufferPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
	glGenRenderbuffersPROC glGenRenderbuffers = (glGenRenderbuffersPROC)SDL_GL_GetProcAddress("glGenRenderbuffers");
	glBindRenderbufferPROC glBindRenderbuffer = (glBindRenderbufferPROC)SDL_GL_GetProcAddress("glBindRenderbuffer");
	glRenderbufferStoragePROC glRenderbufferStorage = (glRenderbufferStoragePROC)SDL_GL_GetProcAddress("glRenderbufferStorage");
	glFramebufferRenderbufferPROC glFramebufferRenderbuffer = (glFramebufferRenderbufferPROC)SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
	glCheckFramebufferStatusPROC glCheckFramebufferStatus = (glCheckFramebufferStatusPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
	glGetIntegervPROC glGetIntegerv = (glGetIntegervPROC)SDL_GL_GetProcAddress("glGetIntegerv");
	
	if(glGenFramebuffers && glBindFramebuffer && glGenRenderbuffers && glBindRenderbuffer && glRenderbufferStorage && glFramebufferRenderbuffer) {
		GLuint fbo = 0;
		GLuint colorRb = 0;
		GLuint depthRb = 0;
		
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(0x8D40, fbo); // GL_FRAMEBUFFER
		
		glGenRenderbuffers(1, &colorRb);
		glBindRenderbuffer(0x8D41, colorRb); // GL_RENDERBUFFER
		glRenderbufferStorage(0x8D41, 0x8058, 640, 480); // GL_RGBA8
		
		glFramebufferRenderbuffer(0x8D40, 0x8CE0, 0x8D41, colorRb); // GL_COLOR_ATTACHMENT0
		
		if(_hwRenderCallback.depth) {
			glGenRenderbuffers(1, &depthRb);
			glBindRenderbuffer(0x8D41, depthRb);
			if(_hwRenderCallback.stencil) {
				glRenderbufferStorage(0x8D41, 0x88F0, 640, 480); // GL_DEPTH24_STENCIL8
				glFramebufferRenderbuffer(0x8D40, 0x821A, 0x8D41, depthRb); // GL_DEPTH_STENCIL_ATTACHMENT
			} else {
				glRenderbufferStorage(0x8D41, 0x81A6, 640, 480); // GL_DEPTH_COMPONENT24
				glFramebufferRenderbuffer(0x8D40, 0x8D00, 0x8D41, depthRb); // GL_DEPTH_ATTACHMENT
			}
		}
		
		_glFramebuffer = fbo;
		
		// Unbind for now
		glBindFramebuffer(0x8D40, 0);
	}
	
	// Call the core's context reset callback
	if(_hwRenderCallback.context_reset) {
		// Make sure GL context is current
		SDL_GL_MakeCurrent(_glWindow, _glContext);
		
		// Clear any GL errors
		typedef unsigned int GLenum;
		typedef GLenum (*glGetErrorPROC)(void);
		glGetErrorPROC glGetError = (glGetErrorPROC)SDL_GL_GetProcAddress("glGetError");
		if(glGetError) {
			while(glGetError() != 0) {} // Clear all errors
		}
		
		// Call the callback with exception handling
		try {
			_hwRenderCallback.context_reset();
		} catch(...) {
			return false;
		}
	}
	
	return true;
#else
	return false;
#endif
}

void LibretroCore::DestroyOpenGLContext()
{
#ifdef USE_SDL
	// Check if we have valid context and window
	if(!_glContext || !_glWindow) {
		return;
	}
	
	// Make the context current before destroying
	SDL_GL_MakeCurrent(_glWindow, _glContext);
	
	// Skip context_destroy callback during reload - the core should have cleaned up
	// in retro_unload_game. Calling context_destroy after retro_unload_game can cause
	// crashes because the core's OpenGL resources may already be partially destroyed.
	// 
	// The context_destroy callback is meant to be called when the context is lost
	// (e.g., when switching to a different renderer), not during normal shutdown.
	
	// Clear the callback to prevent any further calls
	_hwRenderCallback.context_reset = nullptr;
	_hwRenderCallback.context_destroy = nullptr;
	
	if(_glContext) {
		SDL_GL_DeleteContext(_glContext);
		_glContext = nullptr;
	}
	
	// Only destroy the window if we created it (not shared from SdlRenderer)
	if(_glWindow && _glWindow != _sdlWindow) {
		SDL_DestroyWindow(_glWindow);
	}
	_glWindow = nullptr;
	
	_glFramebuffer = 0;
	_useHwRender = false;
#endif
}

uintptr_t LibretroCore::HwGetCurrentFramebuffer()
{
	// Return the framebuffer we created during initialization
	return _instance ? _instance->_glFramebuffer : 0;
}

void* LibretroCore::HwGetProcAddress(const char* sym)
{
#ifdef USE_SDL
	if(!_instance) return nullptr;
	
	SDL_GL_MakeCurrent(_instance->_glWindow, _instance->_glContext);
	
	return SDL_GL_GetProcAddress(sym);
#else
	return nullptr;
#endif
}

void LibretroCore::HwContextReset()
{
	if(_instance && _instance->_hwRenderCallback.context_reset) {
		_instance->_hwRenderCallback.context_reset();
	}
}

void LibretroCore::HwContextDestroy()
{
	if(_instance && _instance->_hwRenderCallback.context_destroy) {
		_instance->_hwRenderCallback.context_destroy();
	}
}
