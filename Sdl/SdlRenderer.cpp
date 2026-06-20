#include "SdlRenderer.h"
#include "Core/Debugger/Debugger.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Core/Shared/Video/VideoDecoder.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/Movies/MovieManager.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/Interfaces/INotificationListener.h"
#include "Core/Shared/NotificationManager.h"
#include "Core/Shared/RenderedFrame.h"
#include "Core/Libretro/LibretroCore.h"
#include "Core/Shared/BaseControlManager.h"
#include "Core/Shared/BaseControlDevice.h"
#include "Core/Shared/ControlDeviceState.h"
#include "Core/Shared/RewindManager.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/Audio/AudioPlayer.h"

// OSD font sizing: reference size at 1.0 scale, and minimum pixel size.
static constexpr int OSD_FONT_SIZE_REF = 13;
static constexpr int OSD_FONT_SIZE_MIN = 8;

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#ifdef _WIN32
#include <windows.h>
#endif

SimpleLock SdlRenderer::_frameLock;
SdlRenderer* SdlRenderer::_osdInstance = nullptr;

// SDL event watch callback: forwards events to ImGui so the OSD menu
// receives mouse input even though the Avalonia UI owns the event loop.
// Keyboard events are injected separately via GetAsyncKeyState because
// Avalonia consumes key events before they reach SDL's queue.
static int SDLCALL osd_event_watch(void *userdata, SDL_Event *event)
{
	SdlRenderer *renderer = static_cast<SdlRenderer*>(userdata);
	if(renderer && renderer->IsOsdVisible()) {
		ImGui_ImplSDL2_ProcessEvent(event);
	}
	return 1; // always let the event continue propagating
}

// Feed keyboard state to ImGui using Win32 GetAsyncKeyState.
// This bypasses the SDL event queue (which Avalonia owns) so the OSD
// menu always receives keyboard input regardless of focus / event routing.
static void osd_inject_keyboard_state()
{
#ifdef _WIN32
	ImGuiIO &io = ImGui::GetIO();

	// Map Win32 virtual keys to ImGui keys.
	// Only the keys the OSD menu actually uses are listed here.
	struct KeyMapping { int vk; ImGuiKey imguiKey; };
	static const KeyMapping keys[] = {
		{ VK_UP,        ImGuiKey_UpArrow    },
		{ VK_DOWN,      ImGuiKey_DownArrow  },
		{ VK_LEFT,      ImGuiKey_LeftArrow  },
		{ VK_RIGHT,     ImGuiKey_RightArrow },
		{ VK_HOME,      ImGuiKey_Home       },
		{ VK_END,       ImGuiKey_End        },
		{ VK_RETURN,    ImGuiKey_Enter      },
		{ VK_ESCAPE,    ImGuiKey_Escape     },
		{ VK_SPACE,     ImGuiKey_Space      },
		{ VK_BACK,      ImGuiKey_Backspace  },
		{ VK_DELETE,    ImGuiKey_Delete     },
		{ VK_PRIOR,     ImGuiKey_PageUp     },
		{ VK_NEXT,      ImGuiKey_PageDown   },
		{ 'A',          ImGuiKey_A          },
		{ 'C',          ImGuiKey_C          },
		{ 'V',          ImGuiKey_V          },
		{ 'X',          ImGuiKey_X          },
		{ 'Y',          ImGuiKey_Y          },
		{ 'Z',          ImGuiKey_Z          },
	};

	for(const auto &km : keys) {
		bool down = (GetAsyncKeyState(km.vk) & 0x8000) != 0;
		io.AddKeyEvent(km.imguiKey, down);
	}

	// Modifier keys - use individual left/right keys since this ImGui
	// version doesn't have ImGuiKey_Mod* aliases.
	io.AddKeyEvent(ImGuiKey_LeftShift,  (GetAsyncKeyState(VK_LSHIFT)   & 0x8000) != 0);
	io.AddKeyEvent(ImGuiKey_RightShift, (GetAsyncKeyState(VK_RSHIFT)   & 0x8000) != 0);
	io.AddKeyEvent(ImGuiKey_LeftCtrl,   (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0);
	io.AddKeyEvent(ImGuiKey_RightCtrl,  (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0);
	io.AddKeyEvent(ImGuiKey_LeftAlt,    (GetAsyncKeyState(VK_LMENU)    & 0x8000) != 0);
	io.AddKeyEvent(ImGuiKey_RightAlt,   (GetAsyncKeyState(VK_RMENU)    & 0x8000) != 0);
	io.AddKeyEvent(ImGuiKey_LeftSuper,  (GetAsyncKeyState(VK_LWIN)     & 0x8000) != 0);
	io.AddKeyEvent(ImGuiKey_RightSuper, (GetAsyncKeyState(VK_RWIN)     & 0x8000) != 0);
#endif
}

SdlRenderer::SdlRenderer(Emulator* emu, void* windowHandle) : _windowHandle(windowHandle)
{
	_emu = emu;
	_frameBuffer = nullptr;
	_requiredWidth = 256;
	_requiredHeight = 240;
	
	_emu->GetVideoRenderer()->RegisterRenderingDevice(this);
}

SdlRenderer::~SdlRenderer()
{
	_emu->GetVideoRenderer()->UnregisterRenderingDevice(this);

	ShutdownOsd();
	Cleanup();
	delete[] _frameBuffer;	
}

void SdlRenderer::LogSdlError(const char* msg)
{
	MessageManager::Log(msg);
	MessageManager::Log(SDL_GetError());
}

void SdlRenderer::SetExclusiveFullscreenMode(bool fullscreen, void* windowHandle)
{
	//TODO: Implement exclusive fullscreen for Linux
}

bool SdlRenderer::Init()
{
	const char* originalHint = SDL_GetHint("SDL_VIDEODRIVER");
	
	#ifdef _WIN32
		SDL_SetHint("SDL_VIDEODRIVER", "windows");
	#else
		SDL_SetHint("SDL_VIDEODRIVER", "x11");
	#endif
	
	if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		LogSdlError("[SDL] Failed to initialize video subsystem.");
		return false;
	};

	// Set OpenGL attributes before creating window (needed for libretro hardware rendering)
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	_sdlWindow = SDL_CreateWindowFrom(_windowHandle);
	if(!_sdlWindow) {
		#ifdef _WIN32
			MessageManager::Log("[SDL] Failed to create window from handle with SDL_VIDEODRIVER=windows, retry with default...");
		#else
			MessageManager::Log("[SDL] Failed to create window from handle with SDL_VIDEODRIVER=x11, retry with default...");
		#endif

		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_SetHint("SDL_VIDEODRIVER", originalHint);
		if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
			LogSdlError("[SDL] Failed to initialize video subsystem.");
			return false;
		}

		// Set OpenGL attributes again after reinit
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

		_sdlWindow = SDL_CreateWindowFrom(_windowHandle);
		if(!_sdlWindow) {
			LogSdlError("[SDL] Failed to create window from handle.");
			return false;
		} else {
			MessageManager::Log("[SDL] Window creation succeeded with default driver.");
		}
	}

	if(SDL_GL_LoadLibrary(NULL) != 0) {
		LogSdlError("[SDL] Failed to initialize OpenGL, attempting to continue with initialization.");
	}

	uint32_t baseFlags = _vsyncEnabled ? SDL_RENDERER_PRESENTVSYNC : 0;

	// Check if a libretro core needs OpenGL rendering
	bool needOpenGL = LibretroCore::NeedsOpenGLRenderer() || LibretroCore::GetForceOpenGL();
	
	#ifdef _WIN32
	if(needOpenGL) {
		// Use OpenGL renderer for libretro cores that need hardware rendering
		MessageManager::Log("[SDL] Libretro core needs OpenGL, using OpenGL renderer...");
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
		_sdlRenderer = SDL_CreateRenderer(_sdlWindow, -1, baseFlags | SDL_RENDERER_ACCELERATED);
	} else {
		MessageManager::Log("[SDL] Attempting to create Direct3D renderer...");
		SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
		_sdlRenderer = SDL_CreateRenderer(_sdlWindow, -1, baseFlags | SDL_RENDERER_ACCELERATED);
		
		if(!_sdlRenderer) {
			MessageManager::Log("[SDL] Direct3D failed, trying OpenGL...");
			SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
			_sdlRenderer = SDL_CreateRenderer(_sdlWindow, -1, baseFlags | SDL_RENDERER_ACCELERATED);
		}
		
		if(!_sdlRenderer) {
			MessageManager::Log("[SDL] OpenGL failed, trying default...");
			SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
			_sdlRenderer = SDL_CreateRenderer(_sdlWindow, -1, baseFlags | SDL_RENDERER_ACCELERATED);
		}
	}
	#else
	_sdlRenderer = SDL_CreateRenderer(_sdlWindow, -1, baseFlags | SDL_RENDERER_ACCELERATED);
	#endif
	
	if(!_sdlRenderer) {
		LogSdlError("[SDL] Failed to create accelerated renderer.");

		MessageManager::Log("[SDL] Attempting to create software renderer...");
		_sdlRenderer = SDL_CreateRenderer(_sdlWindow, -1, baseFlags | SDL_RENDERER_SOFTWARE);
		if(!_sdlRenderer) {
			LogSdlError("[SDL] Failed to create software renderer.");
			return false;
		}
	}
	
	SDL_RendererInfo info;
	if(SDL_GetRendererInfo(_sdlRenderer, &info) == 0) {
		string msg = "[SDL] Using renderer: " + string(info.name);
		MessageManager::Log(msg.c_str());
	}

	return true;
}

bool SdlRenderer::InitOsd()
{
	if(!_sdlWindow || !_sdlRenderer) {
		return false;
	}

	if(_osdReady) {
		return true;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr; // don't write imgui.ini

	if(!ImGui_ImplSDL2_InitForSDLRenderer(_sdlWindow, _sdlRenderer)) {
		MessageManager::Log("[OSD] ImGui_ImplSDL2_InitForSDLRenderer failed");
		ImGui::DestroyContext();
		return false;
	}
	if(!ImGui_ImplSDLRenderer2_Init(_sdlRenderer)) {
		MessageManager::Log("[OSD] ImGui_ImplSDLRenderer2_Init failed");
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	osd_core_setup_style();
	osd_core_rebuild_default_font(osd_core_default_font_size());

	// Install host callbacks so menu items like "Exit" can reach the UI.
	static auto osdToggleFullscreen = []() {
		// Forwarded via MessageManager so the Avalonia UI handles the actual
		// window state change. M2 will wire this to a real notification.
	};
	static auto osdRequestExit = []() {
		// M2 will post a notification the C# UI listens for and call MainWindow.Close().
	};
	// Execute an EmulatorShortcut by sending a notification through the
	// emulator's NotificationManager.  The C# UI already listens for
	// ExecuteShortcut notifications and dispatches them to ShortcutHandler.
	static auto osdExecuteShortcut = [](int shortcut) {
		SdlRenderer *self = SdlRenderer::_osdInstance;
		if(self && self->_emu) {
			ExecuteShortcutParams params = {};
			params.Shortcut = (EmulatorShortcut)shortcut;
			self->_emu->GetNotificationManager()->SendNotification(
				ConsoleNotificationType::ExecuteShortcut, &params);
		}
	};
	osd_host_t host{ osdToggleFullscreen, osdRequestExit, osdExecuteShortcut };
	osd_core_set_host(&host);

	// Install an SDL event watch so ImGui receives keyboard/mouse events.
	_osdInstance = this;
	SDL_AddEventWatch(osd_event_watch, this);
	_osdEventWatchInstalled = true;

	_osdReady = true;
	MessageManager::Log("[OSD] ImGui OSD initialized");
	return true;
}

void SdlRenderer::ShutdownOsd()
{
	if(!_osdReady) {
		return;
	}
	// Remove the event watch before shutting down ImGui.
	if(_osdEventWatchInstalled) {
		SDL_DelEventWatch(osd_event_watch, this);
		_osdEventWatchInstalled = false;
	}
	if(_osdInstance == this) {
		_osdInstance = nullptr;
	}
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	_osdReady = false;
	MessageManager::Log("[OSD] ImGui OSD shut down");
}

void SdlRenderer::SetOsdVisible(bool visible)
{
	if(visible && !_osdReady) {
		// Lazy-init the OSD the first time it's toggled on.
		InitOsd();
	}
	if(visible && _osdReady) {
		osd_core_reset_to_menu();
	}
	_osdVisible = visible && _osdReady;
}

bool SdlRenderer::InitTexture()
{
	_sdlTexture = SDL_CreateTexture(_sdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, _frameWidth, _frameHeight);
	if(!_sdlTexture) {
		string msg = "[SDL] Failed to create texture: " + std::to_string(_frameWidth) + "x" + std::to_string(_frameHeight);
		LogSdlError(msg.c_str());
		return false;
	}

	SDL_SetWindowSize(_sdlWindow, _screenWidth, _screenHeight);

	return true;
}

void SdlRenderer::Cleanup()
{
	// ImGui OSD must be shut down before the SDL_Renderer is destroyed.
	ShutdownOsd();

	if(_sdlTexture) {
		SDL_DestroyTexture(_sdlTexture);
		_sdlTexture = nullptr;
	}
	if(_scriptHud.Texture) {
		SDL_DestroyTexture(_scriptHud.Texture);
		_scriptHud.Texture = nullptr;
	}
	if(_sdlRenderer) {
		SDL_DestroyRenderer(_sdlRenderer);
		_sdlRenderer = nullptr;
	}
}

void SdlRenderer::OnRendererThreadStarted()
{
	//SDL stops working if the rendering moves to a new thread
	//Reset everything to make it work again
	Reset();
}

void SdlRenderer::Reset()
{
	Cleanup();
	if(Init()) {
		InitTexture();
		// Re-initialize the OSD now that a fresh SDL_Renderer exists.
		if(InitOsd()) {
			// Preserve visibility across renderer resets.
		}
		_emu->GetVideoRenderer()->RegisterRenderingDevice(this);
	} else {
		Cleanup();
	}
}

void SdlRenderer::RecreateWithOpenGL()
{
	MessageManager::Log("[SDL] Setting up OpenGL for libretro hardware rendering...");
	
	// Force OpenGL flag
	LibretroCore::SetForceOpenGL(true);
	
	// Set the SDL window to nullptr - libretro will create its own OpenGL window
	// This avoids the issue where SDL_CreateWindowFrom doesn't support OpenGL
	LibretroCore::SetSdlWindow(nullptr);
	
	// Keep the existing renderer (don't recreate)
	// The libretro core will handle its own OpenGL context
	MessageManager::Log("[SDL] OpenGL setup complete - libretro will use its own context");
}

void SdlRenderer::SetScreenSize(uint32_t width, uint32_t height)
{
	VideoConfig cfg = _emu->GetSettings()->GetVideoConfig();
	FrameInfo size = _emu->GetVideoRenderer()->GetRendererSize();
	if(_screenHeight != size.Height || _screenWidth != size.Width || _frameHeight != height || _frameWidth != width || _useBilinearInterpolation != cfg.UseBilinearInterpolation || _vsyncEnabled != cfg.VerticalSync) {
		_vsyncEnabled = cfg.VerticalSync;
		_useBilinearInterpolation = cfg.UseBilinearInterpolation;

		_frameHeight = height;
		_frameWidth = width;
		_newFrameBufferSize = width*height;

		_screenHeight = size.Height;
		_screenWidth = size.Width;

		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, _useBilinearInterpolation ? "1" : "0");
		_screenBufferSize = _screenHeight*_screenWidth;

		Reset();
	}	
}

void SdlRenderer::ClearFrame()
{
	auto lock = _frameLock.AcquireSafe();
	if(_frameBuffer == nullptr) { 
		return;
	}

	memset(_frameBuffer, 0, _requiredWidth * _requiredHeight * _bytesPerPixel);
	_frameChanged = true;
}

void SdlRenderer::UpdateFrame(RenderedFrame& frame)
{
	auto lock = _frameLock.AcquireSafe();
	if(_frameBuffer == nullptr || _requiredWidth != frame.Width || _requiredHeight != frame.Height) {
		_requiredWidth = frame.Width;
		_requiredHeight = frame.Height;
		
		delete[] _frameBuffer;
		_frameBuffer = new uint32_t[frame.Width*frame.Height];
		memset(_frameBuffer, 0, frame.Width * frame.Height *4);
	}
	
	memcpy(_frameBuffer, frame.FrameBuffer, frame.Width * frame.Height *_bytesPerPixel);
	_frameChanged = true;	
}

bool SdlRenderer::UpdateHudSize(HudRenderInfo& hud, uint32_t width, uint32_t height)
{
	if(!hud.Texture || hud.Width != width || hud.Height != height) {
		if(hud.Texture) {
			SDL_DestroyTexture(hud.Texture);
		}
		hud.Width = width;
		hud.Height = height;
		hud.Texture = SDL_CreateTexture(_sdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
		SDL_SetTextureBlendMode(hud.Texture, SDL_BLENDMODE_BLEND);
		return true;
	}
	return false;
}

void SdlRenderer::UpdateHudTexture(HudRenderInfo& hud, uint32_t* src)
{
	uint8_t* textureBuffer;
	int rowPitch;
	if(SDL_LockTexture(hud.Texture, nullptr, (void**)&textureBuffer, &rowPitch) == 0) {
		for(uint32_t i = 0, iMax = hud.Height; i < iMax; i++) {
			memcpy(textureBuffer, src, hud.Width * _bytesPerPixel);
			src += hud.Width;
			textureBuffer += rowPitch;
		}
	} else {
		LogSdlError("SDL_LockTexture failed (HUD)");
	}
	SDL_UnlockTexture(hud.Texture);
}

void SdlRenderer::Render(RenderSurfaceInfo& scriptHud)
{
	SetScreenSize(_requiredWidth, _requiredHeight);
	if(!_sdlRenderer || !_sdlTexture) {
		return;
	}

	bool needUpdate = false;
	needUpdate |= UpdateHudSize(_scriptHud, scriptHud.Width, scriptHud.Height);

	if(SDL_RenderClear(_sdlRenderer) != 0) {
		LogSdlError("SDL_RenderClear failed");
	}

	uint8_t *textureBuffer;
	int rowPitch;
	if(SDL_LockTexture(_sdlTexture, nullptr, (void**)&textureBuffer, &rowPitch) == 0) {
		auto frameLock = _frameLock.AcquireSafe();
		if(_frameBuffer && _frameWidth == _requiredWidth && _frameHeight == _requiredHeight) {
			uint32_t* ppuFrameBuffer = _frameBuffer;
			if(rowPitch != _frameWidth) {
				for(uint32_t i = 0, iMax = _frameHeight; i < iMax; i++) {
					memcpy(textureBuffer, ppuFrameBuffer, _frameWidth*_bytesPerPixel);
					ppuFrameBuffer += _frameWidth;
					textureBuffer += rowPitch;
				}
			} else {
				memcpy(textureBuffer, ppuFrameBuffer, _frameHeight * _frameWidth * _bytesPerPixel);
			}
		}
	} else {
		LogSdlError("SDL_LockTexture failed");
	}
	
	SDL_UnlockTexture(_sdlTexture);

	if(needUpdate || scriptHud.IsDirty) {
		UpdateHudTexture(_scriptHud, scriptHud.Buffer);
	}

	SDL_Rect source = {0, 0, (int)_frameWidth, (int)_frameHeight };
	SDL_Rect dest = {0, 0, (int)_screenWidth, (int)_screenHeight };
	
	if(SDL_RenderCopy(_sdlRenderer, _sdlTexture, &source, &dest) != 0) {
		LogSdlError("SDL_RenderCopy failed");	
	}

	SDL_Rect scriptHudSource = { 0, 0, (int)_scriptHud.Width, (int)_scriptHud.Height };
	if(SDL_RenderCopy(_sdlRenderer, _scriptHud.Texture, &scriptHudSource, &dest) != 0) {
		LogSdlError("SDL_RenderCopy failed (_scriptHud)");
	}

	// ImGui overlay: always render when OSD is initialized so the HUD layer
	// (FPS, messages, status icons) is visible even with the menu closed.
	// The OSD menu (osd_core_build_ui) is only drawn when _osdVisible is set.
	if(_osdReady) {
		// Update OSD layout scale when the output size changes.
		if((int)_screenWidth != _osdLastScreenWidth || (int)_screenHeight != _osdLastScreenHeight) {
			float newScale = osd_core_layout_scale_for_output((int)_screenWidth, (int)_screenHeight);
			osd_core_set_layout_scale(newScale);
			int fontSize = std::max(OSD_FONT_SIZE_MIN,
				(int)std::round(OSD_FONT_SIZE_REF * newScale));
			osd_core_rebuild_default_font(fontSize);
			_osdLastScreenWidth = (int)_screenWidth;
			_osdLastScreenHeight = (int)_screenHeight;
		}

		// Feed emulator state snapshot for HUD rendering.
		if(_emu) {
			// Update FPS counter (mirrors SystemHud::UpdateHud logic).
			if(_emu->IsRunning() && _osdFpsTimer.GetElapsedMS() > 1000) {
				uint32_t frameCount = _emu->GetFrameCount();
				if(_osdLastFrameCount > frameCount) {
					_osdCurrentFps = 0;
				} else {
					_osdCurrentFps = (uint32_t)std::round(
						(double)(frameCount - _osdLastFrameCount) /
						(_osdFpsTimer.GetElapsedMS() / 1000.0));
				}
				_osdLastFrameCount = frameCount;
				_osdFpsTimer.Reset();
			}
			if(_osdCurrentFps > 5000) _osdCurrentFps = 0;

			// Track frame time for DebugStats display.
			// Use the same measurement as the original DebugStats (Emulator::_lastFrameTimer).
			if(_emu->IsRunning()) {
				double elapsed = _emu->GetLastFrameTime();
				if(elapsed > 0 && elapsed < 100) {
					_osdLastFrameTimeMs = elapsed;
					_osdFrameDurations[_osdFrameDurationIndex] = elapsed;
					_osdFrameDurationIndex = (_osdFrameDurationIndex + 1) % 60;
					if(_emu->GetFrameCount() > 60) {
						_osdFrameTimeMin = std::min(elapsed, _osdFrameTimeMin);
						_osdFrameTimeMax = std::max(elapsed, _osdFrameTimeMax);
					} else {
						_osdFrameTimeMin = 9999;
						_osdFrameTimeMax = 0;
					}
				}
			}

			osd_emu_state_t state = {};
			state.is_running = _emu->IsRunning();
			state.is_paused = _emu->IsPaused();
			state.fps = _osdCurrentFps;
			state.frame_count = _emu->GetFrameCount();
			state.lag_count = _emu->GetLagCounter();
			state.fps_rate = _emu->GetFps();
			state.is_turbo = _emu->GetSettings()->CheckFlag(EmulationFlags::Turbo);
			state.is_rewind = _emu->GetSettings()->CheckFlag(EmulationFlags::Rewind);
			state.is_movie_playing = _emu->GetMovieManager()->Playing();
			state.is_movie_recording = _emu->GetMovieManager()->Recording();
			PreferencesConfig cfg = _emu->GetSettings()->GetPreferences();
			state.show_fps = cfg.ShowFps;
			state.show_game_timer = cfg.ShowGameTimer;
			state.show_frame_counter = cfg.ShowFrameCounter;
			state.show_lag_counter = cfg.ShowLagCounter;
			state.show_turbo_rewind_icons = cfg.ShowTurboRewindIcons;
			state.show_movie_icons = cfg.ShowMovieIcons;
			state.show_debug_info = cfg.ShowDebugInfo;
			osd_core_set_emu_state(&state);

			// Feed controller states for InputHud display.
			{
				auto console = _emu->GetConsole();
				if(console) {
					auto ctrlManager = console->GetControlManager();
					vector<ControllerData> portStates = ctrlManager->GetPortStates();
					osd_controller_t osd_ctrl[OSD_MAX_CONTROLLERS];
					int count = 0;
					for(auto& cd : portStates) {
						if(count >= OSD_MAX_CONTROLLERS) break;
						osd_ctrl[count] = {};
						osd_ctrl[count].port = cd.Port;

						// Determine layout from controller type
						switch(cd.Type) {
							case ControllerType::NesController:
							case ControllerType::FamicomController:
							case ControllerType::FamicomControllerP2:
								osd_ctrl[count].layout = OSD_LAYOUT_NES;
								// NES SetBit: Up=0,Down=1,Left=2,Right=3,Start=4,Select=5,B=6,A=7
								if(cd.State.State.size() >= 1) {
									uint8_t raw = cd.State.State[0];
									osd_ctrl[count].buttons[0] = (raw >> 0) & 1; // Up
									osd_ctrl[count].buttons[1] = (raw >> 1) & 1; // Down
									osd_ctrl[count].buttons[2] = (raw >> 2) & 1; // Left
									osd_ctrl[count].buttons[3] = (raw >> 3) & 1; // Right
									osd_ctrl[count].buttons[4] = (raw >> 5) & 1; // Select
									osd_ctrl[count].buttons[5] = (raw >> 4) & 1; // Start
									osd_ctrl[count].buttons[6] = (raw >> 6) & 1; // B
									osd_ctrl[count].buttons[7] = (raw >> 7) & 1; // A
								}
								break;

							case ControllerType::SnesController:
							case ControllerType::SnesRumbleController:
								osd_ctrl[count].layout = OSD_LAYOUT_SNES;
								// SNES SetBit: A=0,B=1,X=2,Y=3,L=4,R=5,Select=6,Start=7,Up=8,Down=9,Left=10,Right=11
								if(cd.State.State.size() >= 2) {
									uint8_t raw0 = cd.State.State[0];
									uint8_t raw1 = cd.State.State[1];
									osd_ctrl[count].buttons[0] = (raw1 >> 0) & 1;  // Up
									osd_ctrl[count].buttons[1] = (raw1 >> 1) & 1;  // Down
									osd_ctrl[count].buttons[2] = (raw1 >> 2) & 1;  // Left
									osd_ctrl[count].buttons[3] = (raw1 >> 3) & 1;  // Right
									osd_ctrl[count].buttons[4] = (raw0 >> 6) & 1;  // Select
									osd_ctrl[count].buttons[5] = (raw0 >> 7) & 1;  // Start
									osd_ctrl[count].buttons[6] = (raw0 >> 1) & 1;  // B
									osd_ctrl[count].buttons[7] = (raw0 >> 0) & 1;  // A
									osd_ctrl[count].buttons[8] = (raw0 >> 3) & 1;  // Y
									osd_ctrl[count].buttons[9] = (raw0 >> 2) & 1;  // X
									osd_ctrl[count].buttons[10] = (raw0 >> 4) & 1; // L
									osd_ctrl[count].buttons[11] = (raw0 >> 5) & 1; // R
								}
								break;

							case ControllerType::GameboyController:
							case ControllerType::GameboyAccelerometer:
								osd_ctrl[count].layout = OSD_LAYOUT_NES;
								// GB SetBit: Up=0,Down=1,Left=2,Right=3,Start=4,Select=5,B=6,A=7
								if(cd.State.State.size() >= 1) {
									uint8_t raw = cd.State.State[0];
									osd_ctrl[count].buttons[0] = (raw >> 0) & 1; // Up
									osd_ctrl[count].buttons[1] = (raw >> 1) & 1; // Down
									osd_ctrl[count].buttons[2] = (raw >> 2) & 1; // Left
									osd_ctrl[count].buttons[3] = (raw >> 3) & 1; // Right
									osd_ctrl[count].buttons[4] = (raw >> 5) & 1; // Select
									osd_ctrl[count].buttons[5] = (raw >> 4) & 1; // Start
									osd_ctrl[count].buttons[6] = (raw >> 6) & 1; // B
									osd_ctrl[count].buttons[7] = (raw >> 7) & 1; // A
								}
								break;

							case ControllerType::GbaController:
								osd_ctrl[count].layout = OSD_LAYOUT_GBA;
								// GBA SetBit: Up=0,Down=1,Left=2,Right=3,Start=4,Select=5,B=6,A=7,L=8,R=9
								if(cd.State.State.size() >= 1) {
									uint8_t raw0 = cd.State.State[0];
									osd_ctrl[count].buttons[0] = (raw0 >> 0) & 1; // Up
									osd_ctrl[count].buttons[1] = (raw0 >> 1) & 1; // Down
									osd_ctrl[count].buttons[2] = (raw0 >> 2) & 1; // Left
									osd_ctrl[count].buttons[3] = (raw0 >> 3) & 1; // Right
									osd_ctrl[count].buttons[4] = (raw0 >> 5) & 1; // Select
									osd_ctrl[count].buttons[5] = (raw0 >> 4) & 1; // Start
									osd_ctrl[count].buttons[6] = (raw0 >> 6) & 1; // B
									osd_ctrl[count].buttons[7] = (raw0 >> 7) & 1; // A
								}
								if(cd.State.State.size() >= 2) {
									uint8_t raw1 = cd.State.State[1];
									osd_ctrl[count].buttons[10] = (raw1 >> 0) & 1; // L
									osd_ctrl[count].buttons[11] = (raw1 >> 1) & 1; // R
								}
								break;

							case ControllerType::PceController:
							case ControllerType::PceTurboTap:
							case ControllerType::PceAvenuePad6:
								osd_ctrl[count].layout = OSD_LAYOUT_PCE;
								// PCE SetBit: Up=0,Down=1,Left=2,Right=3,Select=4,Run=5,I=6,II=7
								if(cd.State.State.size() >= 1) {
									uint8_t raw = cd.State.State[0];
									osd_ctrl[count].buttons[0] = (raw >> 0) & 1; // Up
									osd_ctrl[count].buttons[1] = (raw >> 1) & 1; // Down
									osd_ctrl[count].buttons[2] = (raw >> 2) & 1; // Left
									osd_ctrl[count].buttons[3] = (raw >> 3) & 1; // Right
									osd_ctrl[count].buttons[4] = (raw >> 4) & 1; // Select
									osd_ctrl[count].buttons[5] = (raw >> 5) & 1; // Run
									osd_ctrl[count].buttons[6] = (raw >> 6) & 1; // I
									osd_ctrl[count].buttons[7] = (raw >> 7) & 1; // II
								}
								break;

							case ControllerType::SmsController:
								osd_ctrl[count].layout = OSD_LAYOUT_SMS;
								// SMS SetBit: Up=0,Down=1,Left=2,Right=3,B=4,A=5,Pause=6
								if(cd.State.State.size() >= 1) {
									uint8_t raw = cd.State.State[0];
									osd_ctrl[count].buttons[0] = (raw >> 0) & 1; // Up
									osd_ctrl[count].buttons[1] = (raw >> 1) & 1; // Down
									osd_ctrl[count].buttons[2] = (raw >> 2) & 1; // Left
									osd_ctrl[count].buttons[3] = (raw >> 3) & 1; // Right
									osd_ctrl[count].buttons[6] = (raw >> 4) & 1; // B
									osd_ctrl[count].buttons[7] = (raw >> 5) & 1; // A
									osd_ctrl[count].buttons[5] = (raw >> 6) & 1; // Pause
								}
								break;

							case ControllerType::WsController:
							case ControllerType::WsControllerVertical:
								osd_ctrl[count].layout = OSD_LAYOUT_WS;
								// WS SetBit: Up=0,Down=1,Left=2,Right=3,Up2=4,Down2=5,Left2=6,Right2=7,Sound=8,Start=9,B=10,A=11
								if(cd.State.State.size() >= 1) {
									uint8_t raw0 = cd.State.State[0];
									osd_ctrl[count].buttons[0] = (raw0 >> 0) & 1;  // Up
									osd_ctrl[count].buttons[1] = (raw0 >> 1) & 1;  // Down
									osd_ctrl[count].buttons[2] = (raw0 >> 2) & 1;  // Left
									osd_ctrl[count].buttons[3] = (raw0 >> 3) & 1;  // Right
									osd_ctrl[count].buttons[4] = (raw0 >> 4) & 1;  // Up2
									osd_ctrl[count].buttons[5] = (raw0 >> 5) & 1;  // Down2
									osd_ctrl[count].buttons[6] = (raw0 >> 6) & 1;  // Left2
									osd_ctrl[count].buttons[7] = (raw0 >> 7) & 1;  // Right2
								}
								if(cd.State.State.size() >= 2) {
									uint8_t raw1 = cd.State.State[1];
									osd_ctrl[count].buttons[8] = (raw1 >> 0) & 1;  // Sound
									osd_ctrl[count].buttons[9] = (raw1 >> 1) & 1;  // Start
									osd_ctrl[count].buttons[10] = (raw1 >> 2) & 1; // B
									osd_ctrl[count].buttons[11] = (raw1 >> 3) & 1; // A
								}
								break;

							case ControllerType::NdsController:
								osd_ctrl[count].layout = OSD_LAYOUT_NDS;
								// NDS SetBit: Up=0,Down=1,Left=2,Right=3,Start=4,Select=5,B=6,A=7,Y=8,X=9,L=10,R=11
								if(cd.State.State.size() >= 1) {
									uint8_t raw0 = cd.State.State[0];
									osd_ctrl[count].buttons[0] = (raw0 >> 0) & 1;  // Up
									osd_ctrl[count].buttons[1] = (raw0 >> 1) & 1;  // Down
									osd_ctrl[count].buttons[2] = (raw0 >> 2) & 1;  // Left
									osd_ctrl[count].buttons[3] = (raw0 >> 3) & 1;  // Right
									osd_ctrl[count].buttons[4] = (raw0 >> 5) & 1;  // Select
									osd_ctrl[count].buttons[5] = (raw0 >> 4) & 1;  // Start
									osd_ctrl[count].buttons[6] = (raw0 >> 6) & 1;  // B
									osd_ctrl[count].buttons[7] = (raw0 >> 7) & 1;  // A
								}
								if(cd.State.State.size() >= 2) {
									uint8_t raw1 = cd.State.State[1];
									osd_ctrl[count].buttons[8] = (raw1 >> 0) & 1;  // Y
									osd_ctrl[count].buttons[9] = (raw1 >> 1) & 1;  // X
									osd_ctrl[count].buttons[10] = (raw1 >> 2) & 1; // L
									osd_ctrl[count].buttons[11] = (raw1 >> 3) & 1; // R
								}
								break;

							case ControllerType::ThreeDsController:
								osd_ctrl[count].layout = OSD_LAYOUT_3DS;
								// 3DS SetBit: Up=0,Down=1,Left=2,Right=3,Start=4,Select=5,B=6,A=7,Y=8,X=9,L=10,R=11,ZL=12,ZR=13,...
								if(cd.State.State.size() >= 1) {
									uint8_t raw0 = cd.State.State[0];
									osd_ctrl[count].buttons[0] = (raw0 >> 0) & 1;  // Up
									osd_ctrl[count].buttons[1] = (raw0 >> 1) & 1;  // Down
									osd_ctrl[count].buttons[2] = (raw0 >> 2) & 1;  // Left
									osd_ctrl[count].buttons[3] = (raw0 >> 3) & 1;  // Right
									osd_ctrl[count].buttons[4] = (raw0 >> 5) & 1;  // Select
									osd_ctrl[count].buttons[5] = (raw0 >> 4) & 1;  // Start
									osd_ctrl[count].buttons[6] = (raw0 >> 6) & 1;  // B
									osd_ctrl[count].buttons[7] = (raw0 >> 7) & 1;  // A
								}
								if(cd.State.State.size() >= 2) {
									uint8_t raw1 = cd.State.State[1];
									osd_ctrl[count].buttons[8] = (raw1 >> 0) & 1;  // Y
									osd_ctrl[count].buttons[9] = (raw1 >> 1) & 1;  // X
									osd_ctrl[count].buttons[10] = (raw1 >> 2) & 1; // L
									osd_ctrl[count].buttons[11] = (raw1 >> 3) & 1; // R
								}
								if(cd.State.State.size() >= 3) {
									uint8_t raw2 = cd.State.State[2];
									osd_ctrl[count].buttons[12] = (raw2 >> 0) & 1; // ZL
									osd_ctrl[count].buttons[13] = (raw2 >> 1) & 1; // ZR
								}
								break;

							default:
								osd_ctrl[count].layout = OSD_LAYOUT_GENERIC;
								break;
						}
						count++;
					}
					osd_core_set_controllers(osd_ctrl, count);

					// Feed input display preferences
					InputConfig& inputCfg = _emu->GetSettings()->GetInputConfig();
					osd_input_prefs_t prefs = {};
					prefs.display_position = (int)inputCfg.DisplayInputPosition;
					prefs.display_horizontally = inputCfg.DisplayInputHorizontally;
					for(int i = 0; i < 8; i++)
						prefs.display_port[i] = inputCfg.DisplayInputPort[i];
					osd_core_set_input_prefs(&prefs);
				}

			// Feed audio player state for display.
			AudioPlayer* audioPlayer = _emu->GetAudioPlayer();
			if(audioPlayer) {
				osd_audio_player_t ap = {};
				AudioTrackInfo trackInfo = _emu->GetAudioTrackInfo();
				strncpy(ap.game_title, trackInfo.GameTitle.c_str(), sizeof(ap.game_title) - 1);
				strncpy(ap.artist, trackInfo.Artist.c_str(), sizeof(ap.artist) - 1);
				strncpy(ap.comment, trackInfo.Comment.c_str(), sizeof(ap.comment) - 1);
				strncpy(ap.song_title, trackInfo.SongTitle.c_str(), sizeof(ap.song_title) - 1);
				strncpy(ap.rom_filename, _emu->GetRomInfo().RomFile.GetFileName().c_str(), sizeof(ap.rom_filename) - 1);
				ap.track_number = trackInfo.TrackNumber;
				ap.track_count = trackInfo.TrackCount;
				ap.position = trackInfo.Position;
				ap.length = trackInfo.Length;
				ap.fade_length = trackInfo.FadeLength;
				const std::vector<double> &amps = audioPlayer->GetAmplitudes();
				ap.amplitudes = amps.data();
				ap.amplitudes_count = (int)amps.size();
				ap.sample_rate = audioPlayer->GetSampleRate();
				osd_core_set_audio_player(&ap);
			} else {
				osd_core_set_audio_player(nullptr);
			}
			}

			// Feed debug statistics when ShowDebugInfo is enabled.
			if(cfg.ShowDebugInfo) {
				osd_debug_stats_t dbg = {};
				AudioStatistics audioStats = _emu->GetSoundMixer()->GetStatistics();
				AudioConfig audioCfg = _emu->GetSettings()->GetAudioConfig();
				dbg.audio_latency = audioStats.AverageLatency;
				dbg.audio_target_latency = audioCfg.AudioLatency;
				dbg.audio_underruns = audioStats.BufferUnderrunEventCount;
				dbg.audio_buffer_size = audioStats.BufferSize;
				dbg.audio_sample_rate = (uint32_t)(audioCfg.SampleRate * _emu->GetSoundMixer()->GetRateAdjustment());

				// Calculate FPS the same way as original DebugStats: average of last 60 frame durations
				double totalDuration = 0;
				for(int i = 0; i < 60; i++) {
					totalDuration += _osdFrameDurations[i];
				}
				dbg.video_fps = (totalDuration > 0) ? (1000.0 / (totalDuration / 60.0)) : 0;
				dbg.video_last_frame_ms = _osdLastFrameTimeMs;
				dbg.video_min_frame_ms = _osdFrameTimeMin;
				dbg.video_max_frame_ms = _osdFrameTimeMax;
				memcpy(dbg.frame_durations, _osdFrameDurations, sizeof(dbg.frame_durations));

				RewindStats rewindStats = _emu->GetRewindManager()->GetStats();
				dbg.rewind_memory_mb = (double)rewindStats.MemoryUsage / (1024.0 * 1024.0);
				if(rewindStats.HistoryDuration > 0) {
					dbg.rewind_per_minute_mb = dbg.rewind_memory_mb * 3600.0 / rewindStats.HistoryDuration;
				}

				osd_core_set_debug_stats(&dbg);
			}
		}

		ImGui_ImplSDL2_NewFrame();
		osd_inject_keyboard_state();
		ImGui::NewFrame();

		// HUD layer: always drawn (FPS, messages, status icons).
		osd_hud_draw();

		// OSD menu layer: only when toggled on.
		if(_osdVisible) {
			bool keepOpen = osd_core_build_ui();
			if(!keepOpen) {
				_osdVisible = false;
			}
		}

		ImGui::Render();
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), _sdlRenderer);
	}

	SDL_RenderPresent(_sdlRenderer);
}
