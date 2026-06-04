#pragma once
#include "pch.h"
#include "Shared/Video/BaseVideoFilter.h"

class WsConsole;
class Emulator;

class WsDefaultVideoFilter final : public BaseVideoFilter
{
private:
	Emulator* _emu = nullptr;
	WsConsole* _console = nullptr;
	uint32_t _calculatedPalette[0x1000] = {};
	VideoConfig _videoConfig = {};

	FrameInfo _prevFrameSize = {};
	uint16_t* _prevFrame = nullptr;
	bool _blendFrames = false;
	bool _adjustColors = false;

	uint32_t BlendPixels(uint32_t a, uint32_t b);

	void InitLookupTable();

protected:
	FrameInfo GetFrameInfo() override;

	void OnBeforeApplyFilter() override;

public:
	WsDefaultVideoFilter(Emulator* emu, WsConsole* console, bool applyNtscFilter);
	~WsDefaultVideoFilter();

	void ApplyFilter(uint16_t* ppuOutputBuffer) override;
};