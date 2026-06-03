#pragma once

#include "pch.h"
#include <d3d11.h>
#include <memory>
#include <string>

// Forward declarations from librashader
typedef struct _shader_preset* libra_shader_preset_t;
typedef struct _filter_chain_d3d11* libra_d3d11_filter_chain_t;

class LibraShaderManager
{
public:
	LibraShaderManager();
	~LibraShaderManager();

	// Initialize shader manager with D3D11 device and preset file
	bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, const char* presetPath);
	
	// Shutdown and release resources
	void Shutdown();

	// Check if shader is initialized and ready
	bool IsInitialized() const { return _initialized; }

	// Apply shader effect to source texture, output to render target
	bool ApplyShader(
		ID3D11ShaderResourceView* sourceTexture,
		ID3D11RenderTargetView* renderTarget,
		uint32_t width,
		uint32_t height,
		size_t frameCount
	);

	// Get last error message
	const std::string& GetLastError() const { return _lastError; }

private:
	bool LoadPreset(const char* presetPath);
	void SetError(const std::string& error);

	libra_shader_preset_t _preset = nullptr;
	libra_d3d11_filter_chain_t _filterChain = nullptr;
	ID3D11Device* _device = nullptr;
	ID3D11DeviceContext* _context = nullptr;
	bool _initialized = false;
	std::string _lastError;
};
