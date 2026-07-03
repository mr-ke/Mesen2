#pragma once
#include "pch.h"
#include "Shared/Interfaces/IConsole.h"
#include "Shared/SettingTypes.h"
#include "Genesis/GenesisTypes.h"

class Emulator;
class VirtualFile;
class BaseControlManager;
class GenesisControlManager;
class GenesisPsg;
class GenesisYm2612;
class GenesisM68K;
class GenesisZ80;
class GenesisVdp;
class GenesisMemoryManager;

class GenesisConsole final : public IConsole
{
private:
	Emulator* _emu = nullptr;

	//Subsystem instances (created in LoadRom)
	unique_ptr<GenesisControlManager> _controlManager;
	unique_ptr<GenesisPsg> _psg;
	unique_ptr<GenesisYm2612> _ym2612;
	unique_ptr<GenesisM68K> _m68k;
	unique_ptr<GenesisZ80> _z80;
	unique_ptr<GenesisVdp> _vdp;
	unique_ptr<GenesisMemoryManager> _memoryManager;

	//ROM data (owned by the console, passed to memory manager)
	vector<uint8_t> _romData;
	uint8_t* _sram = nullptr;
	uint32_t _sramSize = 0;
	uint32_t _sramStart = 0x200000; //default SRAM window at 2MB boundary
	bool _sramWritable = true;
	bool _sramEnable = false;
	bool _banked = false; //true if cartridge uses banked mapping
	uint8_t _romBank[8] = {}; //bank registers for banked cartridges

	RomFormat _romFormat = RomFormat::Genesis;
	ConsoleRegion _region = ConsoleRegion::Ntsc;
	GenesisRegion _genesisRegion = GenesisRegion::Ntsc;
	GenesisModel _model = GenesisModel::Model1;
	string _filename;
	string _romRegion; //region string from ROM header (e.g. "JUE")

	uint32_t _frameCount = 0;
	uint64_t _masterClock = 0;

	void UpdateRegion();
	void ParseRomHeader(vector<uint8_t>& romData);
	void InitCart(vector<uint8_t>& romData);

public:
	static vector<string> GetSupportedExtensions() { return { ".md", ".gen", ".smd", ".bin" }; }
	static vector<string> GetSupportedSignatures() { return { }; }

	GenesisConsole(Emulator* emu);
	virtual ~GenesisConsole();

	LoadRomResult LoadRom(VirtualFile& romFile) override;

	void Reset() override;
	void RunFrame() override;

	void SaveBattery() override;

	BaseControlManager* GetControlManager() override;
	ConsoleRegion GetRegion() override { return _region; }
	ConsoleType GetConsoleType() override { return ConsoleType::Genesis; }
	vector<CpuType> GetCpuTypes() override;
	RomFormat GetRomFormat() override { return _romFormat; }
	double GetFps() override;
	PpuFrameInfo GetPpuFrame() override;
	BaseVideoFilter* GetVideoFilter(bool getDefaultFilter) override;

	uint64_t GetMasterClock() override;
	uint32_t GetMasterClockRate() override;

	AudioTrackInfo GetAudioTrackInfo() override { return {}; }
	void ProcessAudioPlayerAction(AudioPlayerActionParams p) override {}

	AddressInfo GetAbsoluteAddress(AddressInfo& relAddress) override;
	AddressInfo GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType) override;
	void GetConsoleState(BaseState& state, ConsoleType consoleType) override;

	GenesisRegion GetGenesisRegion() { return _genesisRegion; }
	GenesisModel GetModel() { return _model; }
	GenesisPsg* GetPsg() { return _psg.get(); }
	GenesisYm2612* GetYm2612() { return _ym2612.get(); }
	GenesisVdp* GetVdp() { return _vdp.get(); }
	GenesisM68K* GetM68K() { return _m68k.get(); }
	GenesisZ80* GetZ80() { return _z80.get(); }
	GenesisMemoryManager* GetMemoryManager() { return _memoryManager.get(); }

	void InitializeRam(void* data, uint32_t length);

	void Serialize(Serializer& s) override;
};
