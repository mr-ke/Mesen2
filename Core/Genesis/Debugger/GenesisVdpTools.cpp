#include "pch.h"
#include "Genesis/Debugger/GenesisVdpTools.h"
#include "Genesis/GenesisConsole.h"
#include "Genesis/GenesisVdp.h"
#include "Genesis/GenesisTypes.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/MemoryDumper.h"
#include "Shared/SettingTypes.h"

GenesisVdpTools::GenesisVdpTools(Debugger* debugger, Emulator* emu, GenesisConsole* console) : PpuTools(debugger, emu)
{
	_console = console;
	_vdp = console->GetVdp();
	_emu = emu;
}

FrameInfo GenesisVdpTools::GetTilemapSize(GetTilemapOptions options, BaseState& state)
{
	return { 1, 1 };
}

DebugTilemapInfo GenesisVdpTools::GetTilemap(GetTilemapOptions options, BaseState& baseState, BaseState& ppuToolsState, uint8_t* vram, uint32_t* palette, uint32_t* outBuffer)
{
	DebugTilemapInfo result = {};
	return result;
}

DebugTilemapTileInfo GenesisVdpTools::GetTilemapTileInfo(uint32_t x, uint32_t y, uint8_t* vram, GetTilemapOptions options, BaseState& baseState, BaseState& ppuToolsState)
{
	return {};
}

DebugSpritePreviewInfo GenesisVdpTools::GetSpritePreviewInfo(GetSpritePreviewOptions options, BaseState& state, BaseState& ppuToolsState)
{
	DebugSpritePreviewInfo info = {};
	return info;
}

void GenesisVdpTools::GetSpriteList(GetSpritePreviewOptions options, BaseState& baseState, BaseState& ppuToolsState, uint8_t* vram, uint8_t* oamRam, uint32_t* palette, DebugSpriteInfo outBuffer[], uint32_t* spritePreviews, uint32_t* screenPreview)
{
	//Stub - sprite preview not yet implemented
}

DebugPaletteInfo GenesisVdpTools::GetPaletteInfo(GetPaletteInfoOptions options)
{
	DebugPaletteInfo info = {};
	return info;
}

void GenesisVdpTools::SetPaletteColor(int32_t colorIndex, uint32_t color)
{
	//Stub - palette editing not yet implemented
}
