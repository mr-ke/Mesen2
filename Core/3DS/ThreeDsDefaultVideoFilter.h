#pragma once
#include "pch.h"
#include "Shared/Video/BaseVideoFilter.h"
#include "Shared/SettingTypes.h"

class Emulator;

class ThreeDsDefaultVideoFilter : public BaseVideoFilter
{
private:
	uint32_t _calculatedPalette[0x10000] = {};
	VideoConfig _videoConfig = {};

	void InitLookupTable();

protected:
	FrameInfo GetFrameInfo() override;

public:
	ThreeDsDefaultVideoFilter(Emulator* emu);
	~ThreeDsDefaultVideoFilter();

	void ApplyFilter(uint16_t* ppuOutputBuffer) override;
};
