#pragma once
#include "SDL.h"
#include "Core/Shared/Interfaces/IRenderingDevice.h"
#include "Utilities/SimpleLock.h"
#include "Utilities/Timer.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Core/Shared/RenderedFrame.h"
#include "Core/Shared/Osd/osd_core.hpp"

class Emulator;

struct HudRenderInfo
{
	SDL_Texture* Texture = nullptr;
	uint32_t Width = 0;
	uint32_t Height = 0;
};

class SdlRenderer : public IRenderingDevice
{
private:
	Emulator* _emu;

	void* _windowHandle;
	SDL_Window* _sdlWindow = nullptr;
	SDL_Renderer *_sdlRenderer = nullptr;
	SDL_Texture* _sdlTexture = nullptr;

	HudRenderInfo _scriptHud = {};
	
	bool _useBilinearInterpolation = false;

	static SimpleLock _frameLock;
	uint32_t* _frameBuffer = nullptr;

	const uint32_t _bytesPerPixel = 4;
	uint32_t _screenBufferSize = 0;

	bool _frameChanged = true;

	uint32_t _screenWidth = 0;
	uint32_t _screenHeight = 0;

	uint32_t _requiredHeight = 0;
	uint32_t _requiredWidth = 0;

	uint32_t _frameHeight = 0;
	uint32_t _frameWidth = 0;
	uint32_t _newFrameBufferSize = 0;

	bool _vsyncEnabled = false;

	// OSD (ImGui) overlay state
	bool _osdReady = false;
	bool _osdVisible = false;
	int  _osdLastScreenWidth = 0;
	int  _osdLastScreenHeight = 0;
	bool _osdEventWatchInstalled = false;

	// FPS tracking for ImGui HUD (mirrors SystemHud logic)
	Timer _osdFpsTimer;
	uint32_t _osdLastFrameCount = 0;
	uint32_t _osdCurrentFps = 0;

	// Frame time tracking for DebugStats display
	double _osdLastFrameTimeMs = 0;
	double _osdFrameTimeMin = 9999;
	double _osdFrameTimeMax = 0;
	double _osdFrameDurations[60] = {};
	uint32_t _osdFrameDurationIndex = 0;

	static SdlRenderer* _osdInstance; // for SDL event watch callback

	bool Init();
	bool InitTexture();
	bool InitOsd();
	void ShutdownOsd();
	void Cleanup();
	void LogSdlError(const char* msg);
	void SetScreenSize(uint32_t width, uint32_t height);
	
	bool UpdateHudSize(HudRenderInfo& hud, uint32_t width, uint32_t height);
	void UpdateHudTexture(HudRenderInfo& hud, uint32_t* src);

public:
	SdlRenderer(Emulator* emu, void* windowHandle);
	virtual ~SdlRenderer();

	void ClearFrame() override;
	void UpdateFrame(RenderedFrame& frame) override;
	void Render(RenderSurfaceInfo& scriptHud) override;
	void Reset() override;
	void OnRendererThreadStarted() override;

	void SetExclusiveFullscreenMode(bool fullscreen, void* windowHandle) override;
	
	// Recreate renderer with OpenGL (for libretro cores that need hardware rendering)
	void RecreateWithOpenGL();
	
	// Get the SDL window (for libretro OpenGL context)
	SDL_Window* GetSdlWindow() { return _sdlWindow; }

	// OSD overlay controls
	void SetOsdVisible(bool visible);
	bool IsOsdVisible() const { return _osdVisible; }
};
