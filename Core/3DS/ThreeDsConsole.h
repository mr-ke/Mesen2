#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"
#include "Shared/SettingTypes.h"
#include "Shared/Interfaces/IConsole.h"
#include "Utilities/ISerializable.h"
#include "Core/Libretro/LibretroCore.h"
#include <mutex>
#include <condition_variable>

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

	// Deferred serialization support
	// The 3DS libretro core requires OpenGL context for serialization, which has
	// thread affinity on Windows. Serialization must happen on the emulation thread.
	// When Serialize() is called from the UI thread, it stores the request and waits
	// for the emulation thread to process it in RunFrame().
	std::mutex _serializeMutex;
	std::condition_variable _serializeCV;
	Serializer* _pendingSerializer = nullptr;
	bool _serializeDone = false;

	void DoSerialize(Serializer& s);

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
