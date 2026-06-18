#include "pch.h"
#include "ThreeDsDefaultVideoFilter.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/Video/VideoDecoder.h"

ThreeDsDefaultVideoFilter::ThreeDsDefaultVideoFilter(Emulator* emu) : BaseVideoFilter(emu)
{
	InitLookupTable();
}

ThreeDsDefaultVideoFilter::~ThreeDsDefaultVideoFilter()
{
}

void ThreeDsDefaultVideoFilter::InitLookupTable()
{
	_videoConfig = _emu->GetSettings()->GetVideoConfig();

	for(int i = 0; i < 0x10000; i++) {
		// RGB565 to RGB888
		uint8_t r = ((i >> 11) & 0x1F) << 3;
		uint8_t g = ((i >> 5) & 0x3F) << 2;
		uint8_t b = (i & 0x1F) << 3;
		
		// Extend to full range
		r |= (r >> 5);
		g |= (g >> 6);
		b |= (b >> 5);
		
		_calculatedPalette[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
	}
}

FrameInfo ThreeDsDefaultVideoFilter::GetFrameInfo()
{
	// Use the base frame info set by SendFrame
	return _baseFrameInfo;
}

void ThreeDsDefaultVideoFilter::ApplyFilter(uint16_t* ppuOutputBuffer)
{
	uint32_t* out = GetOutputBuffer();
	uint32_t width = _frameInfo.Width;
	uint32_t height = _frameInfo.Height;
	
	// The libretro core outputs XRGB8888 (32-bit) format
	// We need to copy the 32-bit data directly
	uint32_t* src = (uint32_t*)ppuOutputBuffer;
	
	for(uint32_t i = 0; i < width * height; i++) {
		uint32_t pixel = src[i];
		// Convert from XRGB8888 to ARGB8888 (add full alpha)
		out[i] = pixel | 0xFF000000;
	}
}
