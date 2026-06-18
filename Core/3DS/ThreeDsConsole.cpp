#include "pch.h"
#include "ThreeDsConsole.h"
#include "ThreeDsControlManager.h"
#include "ThreeDsDefaultVideoFilter.h"
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

ThreeDsConsole::ThreeDsConsole(Emulator* emu)
{
	_emu = emu;
	_controlManager.reset(new ThreeDsControlManager(emu, this));
}

ThreeDsConsole::~ThreeDsConsole()
{
	_core.reset();
}

bool ThreeDsConsole::LoadCore()
{
	// Find the azahar libretro core
	string corePath;
	
	// Try multiple paths in order
	vector<string> searchPaths = {
		// Relative to executable directory
		"3rdParty/azahar-dist/lib/azahar_libretro.dll",
		"3rdParty/azahar-dist/lib/azahar_libretro.so",
		"lib/azahar_libretro.dll",
		"lib/azahar_libretro.so",
		// Relative to home folder
		FolderUtilities::CombinePath(FolderUtilities::GetHomeFolder(), "azahar_libretro.dll"),
		FolderUtilities::CombinePath(FolderUtilities::GetHomeFolder(), "azahar_libretro.so"),
	};

	for(const string& path : searchPaths) {
		ifstream testFile(path);
		if(testFile.good()) {
			corePath = path;
			break;
		}
	}

	if(corePath.empty()) {
		MessageManager::DisplayMessage("3DS", "CouldNotFindCore");
		MessageManager::Log("Could not find azahar libretro core. Searched paths:");
		for(const string& path : searchPaths) {
			MessageManager::Log("  " + path);
		}
		return false;
	}

	MessageManager::Log("Loading azahar core from: " + corePath);

	_core.reset(new LibretroCore(_emu, corePath));
	
	if(!_core->LoadCore()) {
		MessageManager::DisplayMessage("3DS", "CouldNotLoadCore", corePath);
		return false;
	}
	
	return true;
}

LoadRomResult ThreeDsConsole::LoadRom(VirtualFile& romFile)
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

void ThreeDsConsole::Reset()
{
	if(_core) {
		_core->Reset();
	}
	_frameCount = 0;
}

void ThreeDsConsole::RunFrame()
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
			sampleRate = 32768; // Default 3DS sample rate
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

void ThreeDsConsole::SaveBattery()
{
	// Battery saves are handled by the libretro core
}

BaseControlManager* ThreeDsConsole::GetControlManager()
{
	return _controlManager.get();
}

ConsoleRegion ThreeDsConsole::GetRegion()
{
	return ConsoleRegion::Ntsc;
}

ConsoleType ThreeDsConsole::GetConsoleType()
{
	return ConsoleType::ThreeDs;
}

double ThreeDsConsole::GetFps()
{
	// 3DS runs at approximately 59.83 FPS (similar to NDS)
	return 59.83;
}

PpuFrameInfo ThreeDsConsole::GetPpuFrame()
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

vector<CpuType> ThreeDsConsole::GetCpuTypes()
{
	// 3DS uses libretro core, debugging is not supported
	// Return empty list to disable debugger
	return {};
}

AddressInfo ThreeDsConsole::GetAbsoluteAddress(AddressInfo& relAddress)
{
	// Memory mapping is handled by the libretro core
	return { -1, MemoryType::None };
}

AddressInfo ThreeDsConsole::GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType)
{
	// Memory mapping is handled by the libretro core
	return { -1, MemoryType::None };
}

uint64_t ThreeDsConsole::GetMasterClock()
{
	// 3DS runs at 268 MHz
	return 0; // We don't track master clock for libretro cores
}

uint32_t ThreeDsConsole::GetMasterClockRate()
{
	return 268000000;
}

BaseVideoFilter* ThreeDsConsole::GetVideoFilter(bool getDefaultFilter)
{
	return new ThreeDsDefaultVideoFilter(_emu);
}

RomFormat ThreeDsConsole::GetRomFormat()
{
	return RomFormat::ThreeDs;
}

AudioTrackInfo ThreeDsConsole::GetAudioTrackInfo()
{
	return {};
}

void ThreeDsConsole::ProcessAudioPlayerAction(AudioPlayerActionParams p)
{
}

void ThreeDsConsole::GetConsoleState(BaseState& state, ConsoleType consoleType)
{
	// State is managed by the libretro core
}

void ThreeDsConsole::Serialize(Serializer& s)
{
	// Save states are handled by the libretro core
	if(_core && _gameLoaded) {
		// Skip serialization when the emulation thread has stopped (e.g., during exit).
		// The 3DS libretro core has thread-local state that becomes invalid after the
		// emu thread exits, causing access violations if retro_serialize is called
		// from the main thread afterward. Manual/auto save states still work because
		// the emu thread is alive (just paused) in those cases.
		if(_emu && _emu->GetEmulationThreadId() == std::thread::id()) {
			return;
		}

		size_t size = _core->GetSerializeSize();
		// Skip serialization if size is 0 or unreasonably large (> 256MB)
		if(size > 0 && size < 256 * 1024 * 1024) {
			vector<uint8_t> stateData;
			if(s.IsSaving()) {
				try {
					stateData.resize(size);
					if(_core->Serialize(stateData.data(), size)) {
						SVVector(stateData);
					}
				} catch(std::exception&) {
					// Serialization failed (out of memory, etc.) - skip save state
				}
			} else {
				SVVector(stateData);
				if(!stateData.empty()) {
					_core->Unserialize(stateData.data(), stateData.size());
				}
			}
		}
	}
}
