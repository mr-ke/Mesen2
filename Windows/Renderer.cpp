#include "Common.h"
#include "Renderer.h"
#include "DirectXTK/SpriteBatch.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/Video/VideoDecoder.h"
#include "Core/Shared/Video/VideoRenderer.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/SettingTypes.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/BaseControlManager.h"
#include "Core/Shared/BaseControlDevice.h"
#include "Core/Shared/ControlDeviceState.h"
#include "Core/Shared/Audio/AudioPlayer.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/Movies/MovieManager.h"
#include "Core/Shared/RewindManager.h"
#include "Core/Shared/NotificationManager.h"
#include "Utilities/UTF8Util.h"

// ImGui headers
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

using namespace DirectX;

// Static instance for WndProc routing
Renderer* Renderer::_osdInstance = nullptr;

// OSD font size constants (matching SdlRenderer)
static constexpr int OSD_FONT_SIZE_REF = 13;
static constexpr int OSD_FONT_SIZE_MIN = 8;

// Forward declare WndProc handler (ImGui needs this)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Renderer::Renderer(Emulator* emu, HWND hWnd)
{
	_emu = emu;
	_hWnd = hWnd;

	SetScreenSize(256, 224);
}

Renderer::~Renderer()
{
	VideoRenderer* videoRenderer = _emu->GetVideoRenderer();
	if(videoRenderer) {
		videoRenderer->UnregisterRenderingDevice(this);
	}
	CleanupDevice();
}

void Renderer::SetExclusiveFullscreenMode(bool fullscreen, void* windowHandle)
{
	if(fullscreen != _fullscreen || _hWnd != (HWND)windowHandle) {
		int counter = _resetCounter;

		_hWnd = (HWND)windowHandle;
		_monitorWidth = _emu->GetSettings()->GetVideoConfig().FullscreenResWidth;
		_monitorHeight = _emu->GetSettings()->GetVideoConfig().FullscreenResHeight;

		_newFullscreen = fullscreen;

		while(_resetCounter <= counter) {
			std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(10));
		}
	}
}

DXGI_FORMAT Renderer::GetTextureFormat()
{
	return _useSrgbTextureFormat ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
}

void Renderer::SetScreenSize(uint32_t width, uint32_t height)
{
	VideoConfig cfg = _emu->GetSettings()->GetVideoConfig();
	FrameInfo rendererSize = _emu->GetVideoRenderer()->GetRendererSize();
	uint32_t refreshRate = _emu->GetFps() < 55 ? cfg.ExclusiveFullscreenRefreshRatePal : cfg.ExclusiveFullscreenRefreshRateNtsc;

	auto needUpdate = [=] {
		return (
			_emuFrameHeight != height ||
			_emuFrameWidth != width ||
			_screenHeight != rendererSize.Height ||
			_screenWidth != rendererSize.Width ||
			_newFullscreen != _fullscreen ||
			_useSrgbTextureFormat != cfg.UseSrgbTextureFormat ||
			(_fullscreen && _fullscreenRefreshRate != refreshRate) ||
			(_fullscreen && (_realScreenHeight != _monitorHeight || _realScreenWidth != _monitorWidth))
		);
	};

	if(needUpdate()) {
		auto frameLock = _frameLock.AcquireSafe();
		auto textureLock = _textureLock.AcquireSafe();
		if(needUpdate()) {
			_emuFrameHeight = height;
			_emuFrameWidth = width;

			bool needReset = _fullscreen != _newFullscreen;
			bool fullscreenResizeMode = _fullscreen && _newFullscreen;

			if(_pSwapChain && _fullscreen && !_newFullscreen) {
				HRESULT hr = _pSwapChain->SetFullscreenState(FALSE, NULL);
				if(FAILED(hr)) {
					MessageManager::Log("SetFullscreenState(FALSE) failed - Error:" + std::to_string(hr));
				}
			}
			
			if(_useSrgbTextureFormat != cfg.UseSrgbTextureFormat) {
				_useSrgbTextureFormat = cfg.UseSrgbTextureFormat;
				needReset = true;
			}

			_fullscreen = _newFullscreen;
			if(_fullscreenRefreshRate != refreshRate) {
				_fullscreenRefreshRate = refreshRate;
				needReset = true;
			}

			_screenHeight = rendererSize.Height;
			_screenWidth = rendererSize.Width;

			if(_fullscreen) {
				if(_realScreenHeight != _monitorHeight) {
					_realScreenHeight = _monitorHeight;
					needReset = true;
				}

				if(_realScreenWidth != _monitorWidth) {
					_realScreenWidth = _monitorWidth;
					needReset = true;
				}

				//Ensure the screen width/height is smaller or equal to the fullscreen resolution, no matter the requested scale
				if(_monitorHeight < _screenHeight || _monitorWidth < _screenWidth) {
					double scale = (double)width / (double)height;
					_screenHeight = _monitorHeight;
					_screenWidth = (uint32_t)(scale * _screenHeight);
					if(_monitorWidth < _screenWidth) {
						_screenWidth = _monitorWidth;
						_screenHeight = (uint32_t)(_screenWidth / scale);
					}
				}
			} else {
				_realScreenHeight = _screenHeight;
				_realScreenWidth = _screenWidth;
			}

			_leftMargin = (_realScreenWidth - _screenWidth) / 2;
			_topMargin = (_realScreenHeight - _screenHeight) / 2;

			_screenBufferSize = _realScreenHeight*_realScreenWidth;

			if(!_pSwapChain || needReset) {
				Reset();
			} else {
				if(fullscreenResizeMode) {
					ResetTextureBuffers();
					CreateEmuTextureBuffers();
				} else {
					ResetTextureBuffers();
					ReleaseRenderTargetView();
					_pSwapChain->ResizeBuffers(1, _realScreenWidth, _realScreenHeight, GetTextureFormat(), 0);
					CreateRenderTargetView();
					CreateEmuTextureBuffers();
				}
			}
		}
	}
}

void Renderer::Reset()
{
	auto lock = _frameLock.AcquireSafe();
	CleanupDevice();
	if(FAILED(InitDevice())) {
		CleanupDevice();
	} else {
		_emu->GetVideoRenderer()->RegisterRenderingDevice(this);
	}

	_resetCounter++;
}

void Renderer::CleanupDevice()
{
	// Cleanup ImGui OSD first (before releasing D3D device)
	ShutdownOsd();

	// Cleanup librashader resources first
	CleanupShaderResources();

	ResetTextureBuffers();
	ReleaseRenderTargetView();
	if(_pSwapChain) {
		_pSwapChain->SetFullscreenState(false, nullptr);
		_pSwapChain->Release();
		_pSwapChain = nullptr;
	}
	if(_pDeviceContext) {
		_pDeviceContext->Release();
		_pDeviceContext = nullptr;
	}
	if(_pd3dDevice) {
		_pd3dDevice->Release();
		_pd3dDevice = nullptr;
	}
	if(_emuHud.Texture) {
		_emuHud.Texture->Release();
		_emuHud.Texture = nullptr;
	}
	if(_emuHud.Shader) {
		_emuHud.Shader->Release();
		_emuHud.Shader = nullptr;
	}
	if(_scriptHud.Texture) {
		_scriptHud.Texture->Release();
		_scriptHud.Texture = nullptr;
	}
	if(_scriptHud.Shader) {
		_scriptHud.Shader->Release();
		_scriptHud.Shader = nullptr;
	}
}

void Renderer::ResetTextureBuffers()
{
	if(_pTexture) {
		_pTexture->Release();
		_pTexture = nullptr;
	}
	if(_pTextureSrv) {
		_pTextureSrv->Release();
		_pTextureSrv = nullptr;
	}

	delete[] _textureBuffer[0];
	_textureBuffer[0] = nullptr;
	delete[] _textureBuffer[1];
	_textureBuffer[1] = nullptr;
}

void Renderer::ReleaseRenderTargetView()
{
	if(_pRenderTargetView) {
		_pRenderTargetView->Release();
		_pRenderTargetView = nullptr;
	}
}

HRESULT Renderer::CreateRenderTargetView()
{
	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	HRESULT hr = _pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	if(FAILED(hr)) {
		MessageManager::Log("SwapChain::GetBuffer() failed - Error:" + std::to_string(hr));
		return hr;
	}

	hr = _pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &_pRenderTargetView);
	pBackBuffer->Release();
	if(FAILED(hr)) {
		MessageManager::Log("D3DDevice::CreateRenderTargetView() failed - Error:" + std::to_string(hr));
		return hr;
	}

	_pDeviceContext->OMSetRenderTargets(1, &_pRenderTargetView, nullptr);

	return S_OK;
}

HRESULT Renderer::CreateEmuTextureBuffers()
{
	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = (FLOAT)_realScreenWidth;
	vp.Height = (FLOAT)_realScreenHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	_pDeviceContext->RSSetViewports(1, &vp);

	_textureBuffer[0] = new uint8_t[_emuFrameWidth*_emuFrameHeight * 4];
	_textureBuffer[1] = new uint8_t[_emuFrameWidth*_emuFrameHeight * 4];
	memset(_textureBuffer[0], 0, _emuFrameWidth*_emuFrameHeight * 4);
	memset(_textureBuffer[1], 0, _emuFrameWidth*_emuFrameHeight * 4);

	_pTexture = CreateTexture(_emuFrameWidth, _emuFrameHeight);
	if(!_pTexture) {
		return S_FALSE;
	}
	_pTextureSrv = GetShaderResourceView(_pTexture);
	if(!_pTextureSrv) {
		return S_FALSE;
	}

	////////////////////////////////////////////////////////////////////////////
	_spriteBatch.reset(new SpriteBatch(_pDeviceContext));

	// Initialize librashader (optional - will fall back to normal rendering if it fails)
	InitShaderResources();

	// Initialize ImGui OSD (optional - will work without if it fails)
	InitOsd();

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT Renderer::InitDevice()
{
	HRESULT hr = S_OK;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = ARRAYSIZE(driverTypes);

	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 1;
	sd.BufferDesc.Width = _realScreenWidth;
	sd.BufferDesc.Height = _realScreenHeight;
	sd.BufferDesc.Format = GetTextureFormat();
	sd.BufferDesc.RefreshRate.Numerator = _fullscreenRefreshRate;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.Flags = _fullscreen ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;
	sd.OutputWindow = _hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_NULL;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
	for(UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++) {
		driverType = driverTypes[driverTypeIndex];
		featureLevel = D3D_FEATURE_LEVEL_11_1;
		hr = D3D11CreateDeviceAndSwapChain(nullptr, driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels, D3D11_SDK_VERSION, &sd, &_pSwapChain, &_pd3dDevice, &featureLevel, &_pDeviceContext);

		/*if(FAILED(hr)) {
			MessageManager::Log("D3D11CreateDeviceAndSwapChain() failed - Error:" + std::to_string(hr));
		}*/

		if(hr == E_INVALIDARG) {
			// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
			featureLevel = D3D_FEATURE_LEVEL_11_0;
			hr = D3D11CreateDeviceAndSwapChain(nullptr, driverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1, D3D11_SDK_VERSION, &sd, &_pSwapChain, &_pd3dDevice, &featureLevel, &_pDeviceContext);
		}

		if(SUCCEEDED(hr)) {
			break;
		}
	}
		
	if(FAILED(hr)) {
		MessageManager::Log("D3D11CreateDeviceAndSwapChain() failed - Error:" + std::to_string(hr));
		return hr;
	}

	if(_fullscreen) {
		hr = _pSwapChain->SetFullscreenState(TRUE, NULL);
		if(FAILED(hr)) {
			MessageManager::Log("SetFullscreenState(true) failed - Error:" + std::to_string(hr));
			MessageManager::Log("Switching back to windowed mode");
			hr = _pSwapChain->SetFullscreenState(FALSE, NULL);
			if(FAILED(hr)) {
				MessageManager::Log("SetFullscreenState(false) failed - Error:" + std::to_string(hr));
				return hr;
			}
		} else {
			//Get actual monitor resolution (which might differ from the one that was requested)
			HMONITOR monitor = MonitorFromWindow(_hWnd, MONITOR_DEFAULTTOPRIMARY);
			MONITORINFO info = {};
			info.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(monitor, &info);

			uint32_t monitorWidth = info.rcMonitor.right - info.rcMonitor.left;
			uint32_t monitorHeight = info.rcMonitor.bottom - info.rcMonitor.top;

			if(_monitorHeight != monitorHeight || _monitorWidth != monitorWidth) {
				MessageManager::Log(
					"Requested resolution (" + std::to_string(_monitorWidth) + "x" + std::to_string(_monitorHeight) 
					+ ") is not available. Resetting to nearest match instead: " +
					std::to_string(monitorWidth) + "x" + std::to_string(monitorHeight)
				);
				_monitorWidth = monitorWidth;
				_monitorHeight = monitorHeight;

				//Make UI wait until this 2nd reset is over
				_resetCounter--;
			}
		}
	}

	hr = CreateRenderTargetView();
	if(FAILED(hr)) {
		return hr;
	}
	hr = CreateEmuTextureBuffers();
	if(FAILED(hr)) {
		return hr;
	}

	return S_OK;
}

ID3D11Texture2D* Renderer::CreateTexture(uint32_t width, uint32_t height)
{
	ID3D11Texture2D* texture;

	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
	desc.ArraySize = 1;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.Format = GetTextureFormat();
	desc.MipLevels = 1;
	desc.MiscFlags = 0;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.Width = width;
	desc.Height = height;
	desc.MiscFlags = 0;

	HRESULT hr = _pd3dDevice->CreateTexture2D(&desc, nullptr, &texture);
	if(FAILED(hr)) {
		MessageManager::Log("D3DDevice::CreateTexture() failed - Error:" + std::to_string(hr));
		return nullptr;
	}
	return texture;
}

ID3D11ShaderResourceView* Renderer::GetShaderResourceView(ID3D11Texture2D* texture)
{
	ID3D11ShaderResourceView *shaderResourceView = nullptr;
	HRESULT hr = _pd3dDevice->CreateShaderResourceView(texture, nullptr, &shaderResourceView);
	if(FAILED(hr)) {
		MessageManager::Log("D3DDevice::CreateShaderResourceView() failed - Error:" + std::to_string(hr));
		return nullptr;
	}

	return shaderResourceView;
}

void Renderer::ClearFrame()
{
	//Clear current output and display black frame
	auto lock = _textureLock.AcquireSafe();
	if(_textureBuffer[0]) {
		//_textureBuffer[0] may be null if directx failed to initialize properly
		memset(_textureBuffer[0], 0, _emuFrameWidth * _emuFrameHeight * sizeof(uint32_t));
		_needFlip = true;
		_frameChanged = true;
	}
}

void Renderer::UpdateFrame(RenderedFrame& frame)
{
	SetScreenSize(frame.Width, frame.Height);

	auto lock = _textureLock.AcquireSafe();
	if(_textureBuffer[0]) {
		//_textureBuffer[0] may be null if directx failed to initialize properly
		memcpy(_textureBuffer[0], frame.FrameBuffer, frame.Width*frame.Height*sizeof(uint32_t));
		_needFlip = true;
		_frameChanged = true;
	}
}

void Renderer::DrawScreen()
{
	//Swap buffers - emulator always writes to _textureBuffer[0], screen always draws _textureBuffer[1]
	if(_needFlip) {
		auto lock = _textureLock.AcquireSafe();
		uint8_t* textureBuffer = _textureBuffer[0];
		_textureBuffer[0] = _textureBuffer[1];
		_textureBuffer[1] = textureBuffer;
		_needFlip = false;

		if(_frameChanged) {
			_frameChanged = false;
		}
	}

	//Copy buffer to texture
	uint32_t bpp = 4;
	uint32_t rowPitch = _emuFrameWidth * bpp;
	D3D11_MAPPED_SUBRESOURCE dd;
	HRESULT hr = _pDeviceContext->Map(_pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &dd);
	if(FAILED(hr)) {
		MessageManager::Log("DeviceContext::Map() failed - Error:" + std::to_string(hr));
		return;
	}
	uint8_t* surfacePointer = (uint8_t*)dd.pData;
	uint8_t* videoBuffer = _textureBuffer[1];
	if(rowPitch != dd.RowPitch) {
		for(uint32_t i = 0, iMax = _emuFrameHeight; i < iMax; i++) {
			memcpy(surfacePointer, videoBuffer, rowPitch);
			videoBuffer += rowPitch;
			surfacePointer += dd.RowPitch;
		}
	} else {
		memcpy(surfacePointer, videoBuffer, rowPitch * _emuFrameHeight);
	}
	_pDeviceContext->Unmap(_pTexture, 0);

	RECT destRect;
	destRect.left = _leftMargin;
	destRect.top = _topMargin;
	destRect.right = _screenWidth+_leftMargin;
	destRect.bottom = _screenHeight+_topMargin;

	_spriteBatch->Draw(_pTextureSrv, destRect);
}

bool Renderer::CreateHudTexture(HudRenderInfo& hud, uint32_t newWidth, uint32_t newHeight)
{
	if(hud.Texture) {
		hud.Texture->Release();
		hud.Texture = nullptr;
	}
	if(hud.Shader) {
		hud.Shader->Release();
		hud.Shader = nullptr;
	}

	hud.Width = newWidth;
	hud.Height = newHeight;

	hud.Texture = CreateTexture(hud.Width, hud.Height);
	if(!hud.Texture) {
		return false;
	}
	hud.Shader = GetShaderResourceView(hud.Texture);
	if(!hud.Shader) {
		return false;
	}

	return true;
}

void Renderer::DrawHud(HudRenderInfo& hud, RenderSurfaceInfo& hudSurface)
{
	uint32_t* hudBuffer = hudSurface.Buffer;
	uint32_t newWidth = hudSurface.Width;
	uint32_t newHeight = hudSurface.Height;

	if(newWidth == 0 && newHeight == 0) {
		return;
	}

	bool needRedraw = hudSurface.IsDirty;
	if(hud.Width != newWidth || hud.Height != newHeight || !hud.Texture || !hud.Shader) {
		needRedraw = true;
		if(!CreateHudTexture(hud, newWidth, newHeight)) {
			return;
		}
	}

	if(needRedraw) {
		//Copy buffer to texture
		uint32_t rowPitch = hud.Width * sizeof(uint32_t);
		D3D11_MAPPED_SUBRESOURCE dd;
		HRESULT hr = _pDeviceContext->Map(hud.Texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &dd);
		if(FAILED(hr)) {
			MessageManager::Log("DeviceContext::Map() failed - Error:" + std::to_string(hr));
			return;
		}
		uint8_t* surfacePointer = (uint8_t*)dd.pData;
		uint8_t* videoBuffer = (uint8_t*)hudBuffer;
		if(rowPitch != dd.RowPitch) {
			for(uint32_t i = 0, iMax = hud.Height; i < iMax; i++) {
				memcpy(surfacePointer, videoBuffer, rowPitch);
				videoBuffer += rowPitch;
				surfacePointer += dd.RowPitch;
			}
		} else {
			memcpy(surfacePointer, videoBuffer, hud.Height * rowPitch);
		}
		_pDeviceContext->Unmap(hud.Texture, 0);
	}
	
	RECT destRect;
	destRect.left = _leftMargin;
	destRect.top = _topMargin;
	destRect.right = _screenWidth + _leftMargin;
	destRect.bottom = _screenHeight + _topMargin;

	_spriteBatch->Draw(hud.Shader, destRect);
}

void Renderer::Render(RenderSurfaceInfo& scriptHud)
{
	auto lock = _frameLock.AcquireSafe();
	if(_newFullscreen != _fullscreen) {
		SetScreenSize(_emuFrameWidth, _emuFrameHeight);
	}

	if(_pDeviceContext == nullptr) {
		//DirectX failed to initialize, try to init
		Reset();
		if(_pDeviceContext == nullptr) {
			//Can't init, prevent crash
			return;
		}
	}

	VideoConfig cfg = _emu->GetSettings()->GetVideoConfig();

	// Check if shader preset has changed
	bool newUseShader = cfg.UseShaderPreset;
	std::string newShaderPreset = cfg.ShaderPreset;
	if(newUseShader != (_currentShaderPreset != "") || (newUseShader && newShaderPreset != _currentShaderPreset)) {
		// Shader preset changed, reload shader
		ReloadShader();
	}

	// Clear the back buffer 
	_pDeviceContext->ClearRenderTargetView(_pRenderTargetView, Colors::Black);

	// Draw screen with or without librashader
	if(_useLibraShader && _shaderManager && _shaderManager->IsInitialized()) {
		DrawScreenWithShader();
	} else {
		_spriteBatch->Begin(SpriteSortMode_Immediate, cfg.UseBilinearInterpolation);
		DrawScreen();
		_spriteBatch->End();
	}

	//Draw HUD
	_spriteBatch->Begin(SpriteSortMode_Immediate, false);
	DrawHud(_scriptHud, scriptHud);
	_spriteBatch->End();

	// ImGui OSD overlay (always render HUD layer when initialized)
	if(_osdReady) {
		RenderOsd();
	}

	// Present the information rendered to the back buffer to the front buffer (the screen)
	HRESULT hr = _pSwapChain->Present(cfg.VerticalSync ? 1 : 0, 0);
	if(FAILED(hr)) {
		MessageManager::Log("SwapChain::Present() failed - Error:" + std::to_string(hr));
		if(hr == DXGI_ERROR_DEVICE_REMOVED) {
			MessageManager::Log("D3DDevice: GetDeviceRemovedReason: " + std::to_string(_pd3dDevice->GetDeviceRemovedReason()));
		}
		MessageManager::Log("Trying to reset DX...");
		Reset();
	}
}

bool Renderer::InitShaderResources()
{
	// Get the directory where the executable (Mesen.exe) is located
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	std::string exeDir = exePath;
	size_t lastSlash = exeDir.find_last_of("\\/");
	if(lastSlash != std::string::npos) {
		exeDir = exeDir.substr(0, lastSlash);
	}

	// Get shader preset from config
	VideoConfig videoConfig = _emu->GetSettings()->GetVideoConfig();
	std::string shaderPath;
	std::string shaderPreset;
	
	if(videoConfig.UseShaderPreset && videoConfig.ShaderPreset[0] != '\0') {
		// Use shader preset from config
		shaderPreset = videoConfig.ShaderPreset;
		shaderPath = exeDir + "\\Shaders\\" + shaderPreset;
	} else {
		// No shader preset selected
		_useLibraShader = false;
		_currentShaderPreset = "";
		return false;
	}
	
	FILE* testFile = fopen(shaderPath.c_str(), "r");
	if(!testFile) {
		// Shader file not found, disable shader
		_useLibraShader = false;
		_currentShaderPreset = "";
		return false;
	}
	fclose(testFile);

	_shaderManager = std::make_unique<LibraShaderManager>();
	if(!_shaderManager->Initialize(_pd3dDevice, _pDeviceContext, shaderPath.c_str())) {
		MessageManager::Log("[Renderer] Failed to initialize librashader: " + _shaderManager->GetLastError());
		_shaderManager.reset();
		_currentShaderPreset = "";
		return false;
	}
	
	// Store current shader preset for change detection
	_currentShaderPreset = shaderPreset;

	// Create output texture for shader processing
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = _realScreenWidth;
	desc.Height = _realScreenHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = GetTextureFormat();
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = _pd3dDevice->CreateTexture2D(&desc, nullptr, &_pShaderOutputTexture);
	if(FAILED(hr)) {
		MessageManager::Log("[Renderer] Failed to create shader output texture: " + std::to_string(hr));
		_shaderManager.reset();
		return false;
	}

	hr = _pd3dDevice->CreateRenderTargetView(_pShaderOutputTexture, nullptr, &_pShaderRenderTarget);
	if(FAILED(hr)) {
		MessageManager::Log("[Renderer] Failed to create shader render target: " + std::to_string(hr));
		if(_pShaderOutputTexture) {
			_pShaderOutputTexture->Release();
			_pShaderOutputTexture = nullptr;
		}
		_shaderManager.reset();
		return false;
	}

	hr = _pd3dDevice->CreateShaderResourceView(_pShaderOutputTexture, nullptr, &_pShaderOutputSrv);
	if(FAILED(hr)) {
		MessageManager::Log("[Renderer] Failed to create shader SRV: " + std::to_string(hr));
		CleanupShaderResources();
		return false;
	}

	_useLibraShader = true;
	_frameCount = 0;
	MessageManager::Log("[Renderer] Librashader initialized successfully");
	return true;
}

void Renderer::CleanupShaderResources()
{
	if(_pShaderOutputSrv) {
		_pShaderOutputSrv->Release();
		_pShaderOutputSrv = nullptr;
	}
	if(_pShaderRenderTarget) {
		_pShaderRenderTarget->Release();
		_pShaderRenderTarget = nullptr;
	}
	if(_pShaderOutputTexture) {
		_pShaderOutputTexture->Release();
		_pShaderOutputTexture = nullptr;
	}
	if(_shaderManager) {
		_shaderManager->Shutdown();
		_shaderManager.reset();
	}
	_useLibraShader = false;
}

void Renderer::ReloadShader()
{
	// Clean up existing shader resources
	CleanupShaderResources();
	
	// Reinitialize shader resources with new config
	InitShaderResources();
}

void Renderer::DrawScreenWithShader()
{
	//Swap buffers - emulator always writes to _textureBuffer[0], screen always draws _textureBuffer[1]
	if(_needFlip) {
		auto lock = _textureLock.AcquireSafe();
		uint8_t* textureBuffer = _textureBuffer[0];
		_textureBuffer[0] = _textureBuffer[1];
		_textureBuffer[1] = textureBuffer;
		_needFlip = false;

		if(_frameChanged) {
			_frameChanged = false;
		}
	}

	//Copy buffer to texture
	uint32_t bpp = 4;
	uint32_t rowPitch = _emuFrameWidth * bpp;
	D3D11_MAPPED_SUBRESOURCE dd;
	HRESULT hr = _pDeviceContext->Map(_pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &dd);
	if(FAILED(hr)) {
		MessageManager::Log("DeviceContext::Map() failed - Error:" + std::to_string(hr));
		return;
	}
	uint8_t* surfacePointer = (uint8_t*)dd.pData;
	uint8_t* videoBuffer = _textureBuffer[1];
	if(rowPitch != dd.RowPitch) {
		for(uint32_t i = 0, iMax = _emuFrameHeight; i < iMax; i++) {
			memcpy(surfacePointer, videoBuffer, rowPitch);
			videoBuffer += rowPitch;
			surfacePointer += dd.RowPitch;
		}
	} else {
		memcpy(surfacePointer, videoBuffer, rowPitch * _emuFrameHeight);
	}
	_pDeviceContext->Unmap(_pTexture, 0);

	// Apply librashader filter chain
	if(_shaderManager && _shaderManager->IsInitialized()) {
		// Clear the shader output render target
		_pDeviceContext->ClearRenderTargetView(_pShaderRenderTarget, Colors::Black);

		// Apply shader filter chain
		bool success = _shaderManager->ApplyShader(
			_pTextureSrv,
			_pShaderRenderTarget,
			_realScreenWidth,
			_realScreenHeight,
			_frameCount++
		);

		if(!success) {
			return;
		}

		// IMPORTANT: Unbind the render target before using the texture as shader resource
		// D3D11 does not allow a texture to be bound as both RTV and SRV simultaneously
		ID3D11RenderTargetView* nullRTV = nullptr;
		_pDeviceContext->OMSetRenderTargets(1, &nullRTV, nullptr);

		// Restore the back buffer as render target for sprite batch
		_pDeviceContext->OMSetRenderTargets(1, &_pRenderTargetView, nullptr);

		// Draw the shader output to the back buffer
		_spriteBatch->Begin(SpriteSortMode_Immediate, false);
		
		RECT destRect;
		destRect.left = _leftMargin;
		destRect.top = _topMargin;
		destRect.right = _screenWidth + _leftMargin;
		destRect.bottom = _screenHeight + _topMargin;

		_spriteBatch->Draw(_pShaderOutputSrv, destRect);
		_spriteBatch->End();
	}
}

// ---------------------------------------------------------------------------
// OSD (ImGui) implementation
// ---------------------------------------------------------------------------

bool Renderer::InitOsd()
{
	if(!_hWnd || !_pd3dDevice || !_pDeviceContext) {
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

	if(!ImGui_ImplWin32_Init(_hWnd)) {
		MessageManager::Log("[OSD] ImGui_ImplWin32_Init failed");
		ImGui::DestroyContext();
		return false;
	}
	if(!ImGui_ImplDX11_Init(_pd3dDevice, _pDeviceContext)) {
		MessageManager::Log("[OSD] ImGui_ImplDX11_Init failed");
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	osd_core_setup_style();
	osd_core_rebuild_default_font(osd_core_default_font_size());

	// Install host callbacks
	static auto osdToggleFullscreen = []() {};
	static auto osdRequestExit = []() {};
	static auto osdExecuteShortcut = [](int shortcut) {
		Renderer *self = Renderer::_osdInstance;
		if(self && self->_emu) {
			ExecuteShortcutParams params = {};
			params.Shortcut = (EmulatorShortcut)shortcut;
			self->_emu->GetNotificationManager()->SendNotification(
				ConsoleNotificationType::ExecuteShortcut, &params);
		}
	};
	osd_host_t host{ osdToggleFullscreen, osdRequestExit, osdExecuteShortcut };
	osd_core_set_host(&host);

	_osdInstance = this;
	_osdReady = true;
	MessageManager::Log("[OSD] ImGui OSD initialized (D3D11)");
	return true;
}

void Renderer::ShutdownOsd()
{
	if(!_osdReady) {
		return;
	}
	if(_osdInstance == this) {
		_osdInstance = nullptr;
	}
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	_osdReady = false;
	MessageManager::Log("[OSD] ImGui OSD shut down");
}

void Renderer::SetOsdVisible(bool visible)
{
	if(visible && !_osdReady) {
		InitOsd();
	}
	if(visible && _osdReady) {
		osd_core_reset_to_menu();
	}
	_osdVisible = visible && _osdReady;
}

LRESULT CALLBACK Renderer::OsdWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if(_osdInstance && _osdInstance->_osdReady) {
		return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
	}
	return 0;
}

void Renderer::FeedOsdState()
{
	if(!_emu) return;

	// Update FPS counter
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

	// Track frame time for DebugStats display
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

	// Feed emulator state
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

	// Feed controller states
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

			switch(cd.Type) {
				case ControllerType::NesController:
				case ControllerType::FamicomController:
				case ControllerType::FamicomControllerP2:
					osd_ctrl[count].layout = OSD_LAYOUT_NES;
					if(cd.State.State.size() >= 1) {
						uint8_t raw = cd.State.State[0];
						osd_ctrl[count].buttons[0] = (raw >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw >> 3) & 1;
						osd_ctrl[count].buttons[4] = (raw >> 5) & 1;
						osd_ctrl[count].buttons[5] = (raw >> 4) & 1;
						osd_ctrl[count].buttons[6] = (raw >> 6) & 1;
						osd_ctrl[count].buttons[7] = (raw >> 7) & 1;
					}
					break;

				case ControllerType::SnesController:
				case ControllerType::SnesRumbleController:
					osd_ctrl[count].layout = OSD_LAYOUT_SNES;
					if(cd.State.State.size() >= 2) {
						uint8_t raw0 = cd.State.State[0];
						uint8_t raw1 = cd.State.State[1];
						osd_ctrl[count].buttons[0] = (raw1 >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw1 >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw1 >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw1 >> 3) & 1;
						osd_ctrl[count].buttons[4] = (raw0 >> 6) & 1;
						osd_ctrl[count].buttons[5] = (raw0 >> 7) & 1;
						osd_ctrl[count].buttons[6] = (raw0 >> 1) & 1;
						osd_ctrl[count].buttons[7] = (raw0 >> 0) & 1;
						osd_ctrl[count].buttons[8] = (raw0 >> 3) & 1;
						osd_ctrl[count].buttons[9] = (raw0 >> 2) & 1;
						osd_ctrl[count].buttons[10] = (raw0 >> 4) & 1;
						osd_ctrl[count].buttons[11] = (raw0 >> 5) & 1;
					}
					break;

				case ControllerType::GameboyController:
				case ControllerType::GameboyAccelerometer:
					osd_ctrl[count].layout = OSD_LAYOUT_NES;
					if(cd.State.State.size() >= 1) {
						uint8_t raw = cd.State.State[0];
						osd_ctrl[count].buttons[0] = (raw >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw >> 3) & 1;
						osd_ctrl[count].buttons[4] = (raw >> 5) & 1;
						osd_ctrl[count].buttons[5] = (raw >> 4) & 1;
						osd_ctrl[count].buttons[6] = (raw >> 6) & 1;
						osd_ctrl[count].buttons[7] = (raw >> 7) & 1;
					}
					break;

				case ControllerType::GbaController:
					osd_ctrl[count].layout = OSD_LAYOUT_GBA;
					if(cd.State.State.size() >= 1) {
						uint8_t raw0 = cd.State.State[0];
						osd_ctrl[count].buttons[0] = (raw0 >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw0 >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw0 >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw0 >> 3) & 1;
						osd_ctrl[count].buttons[4] = (raw0 >> 5) & 1;
						osd_ctrl[count].buttons[5] = (raw0 >> 4) & 1;
						osd_ctrl[count].buttons[6] = (raw0 >> 6) & 1;
						osd_ctrl[count].buttons[7] = (raw0 >> 7) & 1;
					}
					if(cd.State.State.size() >= 2) {
						uint8_t raw1 = cd.State.State[1];
						osd_ctrl[count].buttons[10] = (raw1 >> 0) & 1;
						osd_ctrl[count].buttons[11] = (raw1 >> 1) & 1;
					}
					break;

				case ControllerType::PceController:
				case ControllerType::PceTurboTap:
				case ControllerType::PceAvenuePad6:
					osd_ctrl[count].layout = OSD_LAYOUT_PCE;
					if(cd.State.State.size() >= 1) {
						uint8_t raw = cd.State.State[0];
						osd_ctrl[count].buttons[0] = (raw >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw >> 3) & 1;
						osd_ctrl[count].buttons[4] = (raw >> 4) & 1;
						osd_ctrl[count].buttons[5] = (raw >> 5) & 1;
						osd_ctrl[count].buttons[6] = (raw >> 6) & 1;
						osd_ctrl[count].buttons[7] = (raw >> 7) & 1;
					}
					break;

				case ControllerType::SmsController:
					osd_ctrl[count].layout = OSD_LAYOUT_SMS;
					if(cd.State.State.size() >= 1) {
						uint8_t raw = cd.State.State[0];
						osd_ctrl[count].buttons[0] = (raw >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw >> 3) & 1;
						osd_ctrl[count].buttons[6] = (raw >> 4) & 1;
						osd_ctrl[count].buttons[7] = (raw >> 5) & 1;
						osd_ctrl[count].buttons[5] = (raw >> 6) & 1;
					}
					break;

				case ControllerType::WsController:
				case ControllerType::WsControllerVertical:
					osd_ctrl[count].layout = OSD_LAYOUT_WS;
					if(cd.State.State.size() >= 1) {
						uint8_t raw0 = cd.State.State[0];
						osd_ctrl[count].buttons[0] = (raw0 >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw0 >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw0 >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw0 >> 3) & 1;
						osd_ctrl[count].buttons[4] = (raw0 >> 4) & 1;
						osd_ctrl[count].buttons[5] = (raw0 >> 5) & 1;
						osd_ctrl[count].buttons[6] = (raw0 >> 6) & 1;
						osd_ctrl[count].buttons[7] = (raw0 >> 7) & 1;
					}
					if(cd.State.State.size() >= 2) {
						uint8_t raw1 = cd.State.State[1];
						osd_ctrl[count].buttons[8] = (raw1 >> 0) & 1;
						osd_ctrl[count].buttons[9] = (raw1 >> 1) & 1;
						osd_ctrl[count].buttons[10] = (raw1 >> 2) & 1;
						osd_ctrl[count].buttons[11] = (raw1 >> 3) & 1;
					}
					break;

				case ControllerType::NdsController:
					osd_ctrl[count].layout = OSD_LAYOUT_NDS;
					if(cd.State.State.size() >= 1) {
						uint8_t raw0 = cd.State.State[0];
						osd_ctrl[count].buttons[0] = (raw0 >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw0 >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw0 >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw0 >> 3) & 1;
						osd_ctrl[count].buttons[4] = (raw0 >> 5) & 1;
						osd_ctrl[count].buttons[5] = (raw0 >> 4) & 1;
						osd_ctrl[count].buttons[6] = (raw0 >> 6) & 1;
						osd_ctrl[count].buttons[7] = (raw0 >> 7) & 1;
					}
					if(cd.State.State.size() >= 2) {
						uint8_t raw1 = cd.State.State[1];
						osd_ctrl[count].buttons[8] = (raw1 >> 0) & 1;
						osd_ctrl[count].buttons[9] = (raw1 >> 1) & 1;
						osd_ctrl[count].buttons[10] = (raw1 >> 2) & 1;
						osd_ctrl[count].buttons[11] = (raw1 >> 3) & 1;
					}
					break;

				case ControllerType::ThreeDsController:
					osd_ctrl[count].layout = OSD_LAYOUT_3DS;
					if(cd.State.State.size() >= 1) {
						uint8_t raw0 = cd.State.State[0];
						osd_ctrl[count].buttons[0] = (raw0 >> 0) & 1;
						osd_ctrl[count].buttons[1] = (raw0 >> 1) & 1;
						osd_ctrl[count].buttons[2] = (raw0 >> 2) & 1;
						osd_ctrl[count].buttons[3] = (raw0 >> 3) & 1;
						osd_ctrl[count].buttons[4] = (raw0 >> 5) & 1;
						osd_ctrl[count].buttons[5] = (raw0 >> 4) & 1;
						osd_ctrl[count].buttons[6] = (raw0 >> 6) & 1;
						osd_ctrl[count].buttons[7] = (raw0 >> 7) & 1;
					}
					if(cd.State.State.size() >= 2) {
						uint8_t raw1 = cd.State.State[1];
						osd_ctrl[count].buttons[8] = (raw1 >> 0) & 1;
						osd_ctrl[count].buttons[9] = (raw1 >> 1) & 1;
						osd_ctrl[count].buttons[10] = (raw1 >> 2) & 1;
						osd_ctrl[count].buttons[11] = (raw1 >> 3) & 1;
					}
					if(cd.State.State.size() >= 3) {
						uint8_t raw2 = cd.State.State[2];
						osd_ctrl[count].buttons[12] = (raw2 >> 0) & 1;
						osd_ctrl[count].buttons[13] = (raw2 >> 1) & 1;
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

	// Feed audio player state
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

	// Feed debug statistics when ShowDebugInfo is enabled
	VideoConfig videoCfg = _emu->GetSettings()->GetVideoConfig();
	if(cfg.ShowDebugInfo) {
		osd_debug_stats_t dbg = {};
		AudioStatistics audioStats = _emu->GetSoundMixer()->GetStatistics();
		AudioConfig audioCfg = _emu->GetSettings()->GetAudioConfig();
		dbg.audio_latency = audioStats.AverageLatency;
		dbg.audio_target_latency = audioCfg.AudioLatency;
		dbg.audio_underruns = audioStats.BufferUnderrunEventCount;
		dbg.audio_buffer_size = audioStats.BufferSize;
		dbg.audio_sample_rate = (uint32_t)(audioCfg.SampleRate * _emu->GetSoundMixer()->GetRateAdjustment());

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

void Renderer::RenderOsd()
{
	if(!_osdReady) {
		return;
	}

	// Update OSD layout scale when the output size changes
	if((int)_screenWidth != _osdLastScreenWidth || (int)_screenHeight != _osdLastScreenHeight) {
		float newScale = osd_core_layout_scale_for_output((int)_screenWidth, (int)_screenHeight);
		osd_core_set_layout_scale(newScale);
		int fontSize = std::max(OSD_FONT_SIZE_MIN,
			(int)std::round(OSD_FONT_SIZE_REF * newScale));
		osd_core_rebuild_default_font(fontSize);
		_osdLastScreenWidth = (int)_screenWidth;
		_osdLastScreenHeight = (int)_screenHeight;
	}

	// Feed emulator state
	FeedOsdState();

	// ImGui frame
	ImGui_ImplWin32_NewFrame();
	ImGui_ImplDX11_NewFrame();
	ImGui::NewFrame();

	// HUD layer: always drawn (FPS, messages, status icons)
	osd_hud_draw();

	// OSD menu layer: only when toggled on
	if(_osdVisible) {
		bool keepOpen = osd_core_build_ui();
		if(!keepOpen) {
			_osdVisible = false;
		}
	}

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
