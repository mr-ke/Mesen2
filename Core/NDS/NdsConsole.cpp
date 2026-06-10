#include "pch.h"
#include "NdsConsole.h"
#include "NdsControlManager.h"
#include "NdsDefaultVideoFilter.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/MessageManager.h"
#include "Core/Shared/BatteryManager.h"
#include "Core/Shared/Video/VideoDecoder.h"
#include "Core/Shared/Audio/SoundMixer.h"
#include "Core/Shared/RenderedFrame.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/Serializer.h"
#include "Utilities/FolderUtilities.h"
#include <fstream>

#ifdef _WIN32
	#include <windows.h>
#endif

NdsConsole::NdsConsole(Emulator* emu)
{
	_emu = emu;
	_controlManager.reset(new NdsControlManager(emu, this));
}

NdsConsole::~NdsConsole()
{
	_core.reset();
}

bool NdsConsole::LoadCore()
{
	// Find the melondsds libretro core
	string corePath;
	
	// Try multiple paths in order
	vector<string> searchPaths = {
		// Relative to executable directory
		"3rdParty/melondsds-dist/lib/melondsds_libretro.dll",
		"3rdParty/melondsds-dist/lib/melondsds_libretro.so",
		"lib/melondsds_libretro.dll",
		"lib/melondsds_libretro.so",
		// Relative to home folder
		FolderUtilities::CombinePath(FolderUtilities::GetHomeFolder(), "melondsds_libretro.dll"),
		FolderUtilities::CombinePath(FolderUtilities::GetHomeFolder(), "melondsds_libretro.so"),
	};

	for(const string& path : searchPaths) {
		ifstream testFile(path);
		if(testFile.good()) {
			corePath = path;
			break;
		}
	}

	if(corePath.empty()) {
		MessageManager::DisplayMessage("NDS", "CouldNotFindCore");
		MessageManager::Log("Could not find melondsds libretro core. Searched paths:");
		for(const string& path : searchPaths) {
			MessageManager::Log("  " + path);
		}
		return false;
	}

	MessageManager::Log("Loading melondsds core from: " + corePath);

	_core.reset(new LibretroCore(_emu, corePath));
	
	if(!_core->LoadCore()) {
		MessageManager::DisplayMessage("NDS", "CouldNotLoadCore", corePath);
		return false;
	}
	
	return true;
}

LoadRomResult NdsConsole::LoadRom(VirtualFile& romFile)
{
	// Read ROM data
	vector<uint8_t> romData;
	romFile.ReadFile(romData);
	
	if(romData.empty()) {
		return LoadRomResult::Failure;
	}

	string romPath = romFile.GetFilePath();

	// Try to reuse existing core, otherwise load a new one
	if(_core && _core->IsLoaded()) {
		_core->UnloadGame();
	} else {
		_core.reset();
		if(!LoadCore()) {
			return LoadRomResult::Failure;
		}
	}

	// Load the game
	if(!_core->LoadGame(romPath, romData.data(), romData.size())) {
		return LoadRomResult::Failure;
	}

	_romData = std::move(romData);
	_romPath = romPath;
	_gameLoaded = true;

	return LoadRomResult::Success;
}

void NdsConsole::Reset()
{
	if(_core) {
		_core->Reset();
	}
	_frameCount = 0;
}

void NdsConsole::RunFrame()
{
	if(!_core || !_gameLoaded) {
		return;
	}

	_core->RunFrame();
	_frameCount++;

	// Send the frame to the VideoDecoder
	PpuFrameInfo frameInfo = GetPpuFrame();
	if(frameInfo.FrameBuffer && frameInfo.Width > 0 && frameInfo.Height > 0) {
		RenderedFrame frame(
			frameInfo.FrameBuffer,
			frameInfo.Width,
			frameInfo.Height,
			1.0,
			_frameCount
		);
		_emu->GetVideoDecoder()->UpdateFrame(frame, false, false);
	}

	// Send audio to SoundMixer
	const auto& audioBuffer = _core->GetAudioBuffer();
	if(!audioBuffer.empty()) {
		// Audio buffer contains interleaved stereo samples (L, R, L, R, ...)
		// sampleCount is number of frames (pairs of samples)
		uint32_t sampleCount = audioBuffer.size() / 2;
		uint32_t sampleRate = _core->GetSystemAvInfo().timing.sample_rate;
		
		if(sampleRate == 0) {
			sampleRate = 32768; // Default NDS sample rate
		}
		
		_emu->GetSoundMixer()->PlayAudioBuffer(
			const_cast<int16_t*>(audioBuffer.data()),
			sampleCount,
			sampleRate
		);
		_core->ClearAudioBuffer();
	}

	// Process end of frame (triggers frame limiter)
	_emu->ProcessEndOfFrame();
}

void NdsConsole::SaveBattery()
{
	// Battery saves are handled by the libretro core
}

BaseControlManager* NdsConsole::GetControlManager()
{
	return _controlManager.get();
}

ConsoleRegion NdsConsole::GetRegion()
{
	return ConsoleRegion::Ntsc;
}

ConsoleType NdsConsole::GetConsoleType()
{
	return ConsoleType::Nds;
}

double NdsConsole::GetFps()
{
	// NDS runs at approximately 59.8261 FPS
	return 59.8261;
}

PpuFrameInfo NdsConsole::GetPpuFrame()
{
	PpuFrameInfo frame = {};
	
	if(_core && _gameLoaded) {
		// Cast from uint32_t* to uint8_t* (buffer is 32-bit ARGB)
		frame.FrameBuffer = (uint8_t*)_core->GetFrameBuffer();
		frame.Width = _core->GetFrameWidth();
		frame.Height = _core->GetFrameHeight();
		frame.FrameBufferSize = frame.Width * frame.Height * 4; // 32-bit ARGB
		frame.FrameCount = _frameCount;
	}
	
	return frame;
}

vector<CpuType> NdsConsole::GetCpuTypes()
{
	// NDS uses libretro core, debugging is not supported
	// Return empty list to disable debugger
	return {};
}

AddressInfo NdsConsole::GetAbsoluteAddress(AddressInfo& relAddress)
{
	// Memory mapping is handled by the libretro core
	return { -1, MemoryType::None };
}

AddressInfo NdsConsole::GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType)
{
	// Memory mapping is handled by the libretro core
	return { -1, MemoryType::None };
}

uint64_t NdsConsole::GetMasterClock()
{
	// NDS ARM9 runs at 67.028 MHz
	return 0; // We don't track master clock for libretro cores
}

uint32_t NdsConsole::GetMasterClockRate()
{
	return 67028000;
}

BaseVideoFilter* NdsConsole::GetVideoFilter(bool getDefaultFilter)
{
	return new NdsDefaultVideoFilter(_emu);
}

RomFormat NdsConsole::GetRomFormat()
{
	return RomFormat::Nds;
}

AudioTrackInfo NdsConsole::GetAudioTrackInfo()
{
	return {};
}

void NdsConsole::ProcessAudioPlayerAction(AudioPlayerActionParams p)
{
}

void NdsConsole::GetConsoleState(BaseState& state, ConsoleType consoleType)
{
	// State is managed by the libretro core
}

void NdsConsole::Serialize(Serializer& s)
{
	
	// Save states are handled by the libretro core
	if(_core && _gameLoaded) {
		size_t size = _core->GetSerializeSize();
		if(size > 0) {
			vector<uint8_t> stateData;
			if(s.IsSaving()) {
				stateData.resize(size);
				if(_core->Serialize(stateData.data(), size)) {
					SVVector(stateData);
				} else {
				}
			} else {
				SVVector(stateData);
				if(!stateData.empty()) {
					_core->Unserialize(stateData.data(), stateData.size());
				}
			}
		}
	} else {
	}
}
