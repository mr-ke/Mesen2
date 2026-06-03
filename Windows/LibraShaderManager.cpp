#include "LibraShaderManager.h"
#include "Core/Shared/MessageManager.h"

// Define runtime for librashader D3D11
#define LIBRA_RUNTIME_D3D11
#include "librashader/librashader.h"

LibraShaderManager::LibraShaderManager()
{
}

LibraShaderManager::~LibraShaderManager()
{
	Shutdown();
}

void LibraShaderManager::SetError(const std::string& error)
{
	_lastError = error;
	MessageManager::Log("[LibraShader] " + error);
}

bool LibraShaderManager::LoadPreset(const char* presetPath)
{
	if (!presetPath || presetPath[0] == '\0') {
		SetError("Preset path is empty");
		return false;
	}

	libra_error_t error = libra_preset_create(presetPath, &_preset);
	if (error) {
		char* errorMsg = nullptr;
		libra_error_write(error, &errorMsg);
		if (errorMsg) {
			SetError(std::string("Failed to load preset: ") + errorMsg);
			libra_error_free_string(&errorMsg);
		} else {
			SetError(std::string("Failed to load preset: ") + presetPath);
		}
		libra_error_free(&error);
		return false;
	}

	if (!_preset) {
		SetError("Preset creation returned null");
		return false;
	}

	return true;
}

bool LibraShaderManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, const char* presetPath)
{
	if (_initialized) {
		return true;
	}

	if (!device || !context) {
		SetError("Invalid D3D11 device or context");
		return false;
	}

	_device = device;
	_context = context;

	// Load the shader preset
	if (!LoadPreset(presetPath)) {
		return false;
	}

	// Create D3D11 filter chain options
	filter_chain_d3d11_opt_t options = {};
	options.version = 1;
	options.force_no_mipmaps = false;
	options.disable_cache = false;

	// Create the filter chain
	libra_error_t error = libra_d3d11_filter_chain_create(
		&_preset,
		_device,
		&options,
		&_filterChain
	);

	if (error) {
		char* errorMsg = nullptr;
		libra_error_write(error, &errorMsg);
		if (errorMsg) {
			SetError(std::string("Failed to create filter chain: ") + errorMsg);
			libra_error_free_string(&errorMsg);
		} else {
			SetError("Failed to create filter chain");
		}
		libra_error_free(&error);
		
		if (_preset) {
			libra_preset_free(&_preset);
			_preset = nullptr;
		}
		return false;
	}

	if (!_filterChain) {
		SetError("Filter chain creation returned null");
		if (_preset) {
			libra_preset_free(&_preset);
			_preset = nullptr;
		}
		return false;
	}

	_initialized = true;
	MessageManager::Log("[LibraShader] Initialized successfully with preset: " + std::string(presetPath));
	return true;
}

bool LibraShaderManager::ApplyShader(
	ID3D11ShaderResourceView* sourceTexture,
	ID3D11RenderTargetView* renderTarget,
	uint32_t width,
	uint32_t height,
	size_t frameCount)
{
	if (!_initialized || !_filterChain) {
		SetError("Shader not initialized");
		return false;
	}

	if (!sourceTexture || !renderTarget) {
		SetError("Invalid source texture or render target");
		return false;
	}

	// Set up frame options
	frame_d3d11_opt_t frameOptions = {};
	frameOptions.version = 1;
	frameOptions.clear_history = false;
	frameOptions.frame_direction = 1;  // Forward playback
	frameOptions.rotation = 0;
	frameOptions.total_subframes = 1;
	frameOptions.current_subframe = 1;

	// Apply the shader filter chain
	// Pass nullptr for viewport to use full render target
	libra_error_t error = libra_d3d11_filter_chain_frame(
		&_filterChain,
		_context,
		frameCount,
		sourceTexture,
		renderTarget,
		nullptr,  // Use default viewport (full render target)
		nullptr,  // Use default MVP matrix
		&frameOptions
	);

	if (error) {
		char* errorMsg = nullptr;
		libra_error_write(error, &errorMsg);
		if (errorMsg) {
			SetError(std::string("Filter chain frame error: ") + errorMsg);
			libra_error_free_string(&errorMsg);
		} else {
			SetError("Filter chain frame error");
		}
		libra_error_free(&error);
		return false;
	}

	return true;
}

void LibraShaderManager::Shutdown()
{
	if (_filterChain) {
		libra_d3d11_filter_chain_free(&_filterChain);
		_filterChain = nullptr;
	}

	if (_preset) {
		libra_preset_free(&_preset);
		_preset = nullptr;
	}

	_device = nullptr;
	_context = nullptr;
	_initialized = false;
}
