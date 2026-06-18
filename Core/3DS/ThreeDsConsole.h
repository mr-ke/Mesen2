#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"
#include "Shared/SettingTypes.h"
#include "Shared/Interfaces/IConsole.h"
#include "Utilities/ISerializable.h"
#include "Core/Libretro/LibretroCore.h"

class Emulator;
class BaseControlManager;
class ThreeDsControlManager;
class VirtualFile;

class ThreeDsConsole final : public IConsole
{
private:
	Emulator* _emu = nullptr;
	unique_ptr<LibretroCore> _core;
	unique_ptr<ThreeDsControlManager> _controlManager;
	
	string _romPath;
	vector<uint8_t> _romData;
	
	bool _gameLoaded = false;
	uint32_t _frameCount = 0;

public:
	ThreeDsConsole(Emulator* emu);
	~ThreeDsConsole();

	static vector<string> GetSupportedExtensions() { return { ".cci", ".cia", ".3ds", ".3dsx" }; }
	static vector<string> GetSupportedSignatures() { return { }; }

	// Load the azahar libretro core
	bool LoadCore();
	
	// IConsole interface implementation
	void Reset() override;
	LoadRomResult LoadRom(VirtualFile& romFile) override;
	void RunFrame() override;
	void SaveBattery() override;
	
	BaseControlManager* GetControlManager() override;
	ConsoleRegion GetRegion() override;
	ConsoleType GetConsoleType() override;
	double GetFps() override;
	PpuFrameInfo GetPpuFrame() override;
	vector<CpuType> GetCpuTypes() override;

	AddressInfo GetAbsoluteAddress(AddressInfo& relAddress) override;
	AddressInfo GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType) override;

	uint64_t GetMasterClock() override;
	uint32_t GetMasterClockRate() override;

	BaseVideoFilter* GetVideoFilter(bool getDefaultFilter) override;

	RomFormat GetRomFormat() override;
	AudioTrackInfo GetAudioTrackInfo() override;
	void ProcessAudioPlayerAction(AudioPlayerActionParams p) override;

	void GetConsoleState(BaseState& state, ConsoleType consoleType) override;

	void Serialize(Serializer& s) override;
};
