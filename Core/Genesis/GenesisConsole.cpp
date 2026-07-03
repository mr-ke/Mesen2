#include "pch.h"
#include "Genesis/GenesisConsole.h"
#include "Genesis/GenesisControlManager.h"
#include "Genesis/GenesisPsg.h"
#include "Genesis/GenesisYm2612.h"
#include "Genesis/GenesisM68K.h"
#include "Genesis/GenesisZ80.h"
#include "Genesis/GenesisVdp.h"
#include "Genesis/GenesisMemoryManager.h"
#include "Genesis/GenesisDefaultVideoFilter.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/BatteryManager.h"
#include "Shared/Video/VideoDecoder.h"
#include "Shared/RewindManager.h"
#include "Shared/NotificationManager.h"
#include "Utilities/Serializer.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/CRC32.h"
#include <cmath>

GenesisConsole::GenesisConsole(Emulator* emu)
{
	_emu = emu;
	_controlManager = unique_ptr<GenesisControlManager>(new GenesisControlManager(_emu, this));
	_psg = unique_ptr<GenesisPsg>(new GenesisPsg(_emu, this));
	_ym2612 = unique_ptr<GenesisYm2612>(new GenesisYm2612(_emu, this));
	UpdateRegion();
}

GenesisConsole::~GenesisConsole()
{
	if(_sram) delete[] _sram;
}

LoadRomResult GenesisConsole::LoadRom(VirtualFile& romFile)
{
	vector<uint8_t> romData;
	romFile.ReadFile(romData);

	if(romData.size() < 0x200) {
		return LoadRomResult::Failure;
	}

	//Strip SMD interleaved header if present (512-byte header)
	if((romData.size() % 0x4000) == 512) {
		//SMD format: deinterleave 16KB blocks
		vector<uint8_t> deinterleaved;
		deinterleaved.resize(romData.size() - 512);
		uint32_t blockSize = 0x4000;
		uint32_t numBlocks = (uint32_t)(deinterleaved.size() / blockSize);
		for(uint32_t block = 0; block < numBlocks; block++) {
			for(uint32_t i = 0; i < blockSize; i++) {
				//SMD interleaves odd/even bytes: first 8KB = even bytes, second 8KB = odd bytes
				if(i < blockSize / 2) {
					deinterleaved[block * blockSize + i * 2 + 1] = romData[512 + block * blockSize + i];
				} else {
					deinterleaved[block * blockSize + (i - blockSize / 2) * 2] = romData[512 + block * blockSize + i];
				}
			}
		}
		romData = std::move(deinterleaved);
	}

	_filename = romFile.GetFileName();
	_romFormat = RomFormat::Genesis;

	//Parse the ROM header for SRAM info and region detection
	ParseRomHeader(romData);

	//Initialize cartridge (pad ROM to power-of-2, allocate SRAM)
	InitCart(romData);

	//Create all subsystems
	_vdp = unique_ptr<GenesisVdp>(new GenesisVdp(_emu, this));
	_m68k = unique_ptr<GenesisM68K>(new GenesisM68K());
	_z80 = unique_ptr<GenesisZ80>(new GenesisZ80());
	_memoryManager = unique_ptr<GenesisMemoryManager>(new GenesisMemoryManager());

	//Initialize memory manager (wires bus callbacks to M68K and Z80)
	_memoryManager->Init(
		_emu, this,
		_m68k.get(), _z80.get(), _vdp.get(),
		_psg.get(), _ym2612.get(),
		_controlManager.get(),
		_romData.data(), (uint32_t)_romData.size(),
		_sram, _sramSize, _sramStart, _sramWritable
	);

	//Wire M68K pointer to VDP
	_vdp->SetM68K(_m68k.get());

	//Power on all subsystems
	_vdp->Power();
	_m68k->Power();
	_z80->Power();

	UpdateRegion();

	return LoadRomResult::Success;
}

void GenesisConsole::ParseRomHeader(vector<uint8_t>& romData)
{
	if(romData.size() < 0x200) return;

	//SRAM info from ROM header at offset $1B0-$1BF
	// $1B0: "RA" for SRAM present
	// $1B2: SRAM start address (big-endian 32-bit)
	// $1B6: SRAM end address (big-endian 32-bit)
	// $1BA: SRAM type: bit 7 = 1 for SRAM (vs EEPROM), bit 0 = 1 for odd-byte-only
	if(romData.size() > 0x1BB) {
		if(romData[0x1B0] == 'R' && romData[0x1B1] == 'A') {
			_sramStart = ((uint32_t)romData[0x1B2] << 24) | ((uint32_t)romData[0x1B3] << 16) |
			             ((uint32_t)romData[0x1B4] << 8) | (uint32_t)romData[0x1B5];
			uint32_t sramEnd = ((uint32_t)romData[0x1B6] << 24) | ((uint32_t)romData[0x1B7] << 16) |
			                   ((uint32_t)romData[0x1B8] << 8) | (uint32_t)romData[0x1B9];
			uint8_t sramType = romData[0x1BA];

			if(sramEnd > _sramStart) {
				_sramSize = (sramEnd - _sramStart) + 1; //+1 because end is inclusive
				//Some ROMs report double the actual size (odd+even bytes), halve it
				//for 8-bit SRAM
				if(sramType & 0x01) {
					//Odd-byte SRAM — actual size is half
					_sramSize = (_sramSize + 1) / 2;
				}
				//Cap at 64KB
				if(_sramSize > 0x10000) _sramSize = 0x10000;
				_sramEnable = true;
			}
		}
	}

	//Detect region from ROM header
	//Offset $1F0: domestic/overseas region string (e.g. "JUE" = Japan+US+Europe)
	if(romData.size() > 0x1F4) {
		for(int i = 0; i < 16 && romData[0x1F0 + i]; i++) {
			_romRegion += (char)romData[0x1F0 + i];
		}
	}
}

void GenesisConsole::InitCart(vector<uint8_t>& romData)
{
	//Pad ROM to power-of-2 size (matching SMS pattern)
	uint32_t power = (uint32_t)std::log2(romData.size());
	if(romData.size() > ((uint64_t)1 << power)) {
		uint32_t newSize = 1 << (power + 1);
		romData.insert(romData.end(), newSize - romData.size(), 0xFF);
	}

	//Initialize bank registers (default: identity mapping)
	for(int i = 0; i < 8; i++) {
		_romBank[i] = i;
	}

	//Allocate SRAM if the header indicated its presence
	if(_sramSize > 0 && _sramEnable) {
		_sram = new uint8_t[_sramSize];
		memset(_sram, 0, _sramSize);
	} else {
		_sram = nullptr;
		_sramSize = 0;
		_sramEnable = false;
	}

	//Store ROM data
	_romData = std::move(romData);
}

void GenesisConsole::InitializeRam(void* data, uint32_t length)
{
	EmuSettings* settings = _emu->GetSettings();
	RamState ramState = settings->GetGenesisConfig().RamPowerOnState;
	settings->InitializeRam(ramState, data, length);
}

void GenesisConsole::Reset()
{
	if(_m68k) _m68k->Power();
	if(_z80) _z80->Power();
	if(_vdp) _vdp->Reset();
	if(_memoryManager) _memoryManager->Reset();
	_frameCount = 0;
	_masterClock = 0;
	UpdateRegion();
}

void GenesisConsole::RunFrame()
{
	if(!_m68k || !_vdp || !_memoryManager) {
		_frameCount++;
		return;
	}

	//Update controller devices and poll input state for this frame
	_controlManager->UpdateControlDevices();
	_controlManager->UpdateInputState();

	//Clear the framebuffer to the background color so stale pixels from the
	//previous frame don't persist in regions the VDP doesn't write to.
	_vdp->ClearFramebuffer();

	//Genesis frame timing:
	//NTSC: 262 scanlines per frame, ~59.92 fps
	//PAL:  313 scanlines per frame, ~49.70 fps
	uint32_t scanlinesPerFrame = (_genesisRegion == GenesisRegion::Pal) ? 313 : 262;

	//Run all scanlines in the frame
	//Each scanline: VDP runs first, then M68K runs for the scanline's worth of cycles,
	//then Z80 runs for its share. Interrupts are polled at the start of each scanline.
	uint32_t m68kCyclesPerScanline = (uint32_t)(GetMasterClockRate() / 7.0 / GetFps() / scanlinesPerFrame);
	uint32_t z80CyclesPerScanline = (uint32_t)(GetMasterClockRate() / 15.0 / GetFps() / scanlinesPerFrame);
	uint64_t masterClockPerScanline = (uint64_t)(GetMasterClockRate() / GetFps() / scanlinesPerFrame);

	for(uint32_t line = 0; line < scanlinesPerFrame; line++) {
		//Advance master clock for this scanline so audio chips can generate
		//samples at the correct rate.
		_masterClock += masterClockPerScanline;

		//Run VDP for one scanline (renders pixels, generates Hblank/Vblank)
		_vdp->RunScanline();

		//Wire VDP Hblank interrupt to Z80 IRQ line for audio driver sync
		if(_z80) {
			_z80->SetIrq(_vdp->GetHblankIrq());
		}

		//Note: M68K interrupt polling is now done per-instruction inside
		//ExecuteInstruction (via CheckInterrupts callback), matching ares's
		//behavior where the CPU checks for pending interrupts before each
		//instruction. This is critical for VBlank delivery timing.

		//Run M68K for approximately one scanline's worth of M68K cycles
		uint32_t targetCycles = m68kCyclesPerScanline;
		uint32_t cyclesRun = 0;
		uint32_t m68kMaxInstr = targetCycles * 4; //safety limit
		while(cyclesRun < targetCycles && !_m68k->IsStopped() && m68kMaxInstr-- > 0) {
			cyclesRun += _m68k->ExecuteInstruction();
			cyclesRun += _vdp->ConsumeBusPenalty();
		}

		//Run Z80 for its share of cycles
		if(_z80) {
			uint32_t z80Target = z80CyclesPerScanline;
			uint32_t z80Run = 0;
			uint32_t z80MaxInstr = z80Target * 4; //safety limit
			while(z80Run < z80Target && z80MaxInstr-- > 0) {
				z80Run += _z80->ExecuteInstruction();
			}
		}

		//Run audio chips per-scanline for accurate sample timing
		_psg->Run();
		_ym2612->Run();
	}

	//Flush accumulated audio samples to the sound mixer
	_psg->PlayQueuedAudio();
	_ym2612->PlayQueuedAudio();

	_frameCount++;

	//Send the rendered frame to the video decoder
	PpuFrameInfo ppuFrame = GetPpuFrame();
	RenderedFrame frame(
		ppuFrame.FrameBuffer,
		ppuFrame.Width,
		ppuFrame.Height,
		1.0,
		ppuFrame.FrameCount,
		_controlManager->GetPortStates()
	);
	bool rewinding = _emu->GetRewindManager()->IsRewinding();
	_emu->GetVideoDecoder()->UpdateFrame(frame, rewinding, rewinding);
	_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::PpuFrameDone);
	_emu->ProcessEndOfFrame();
}

void GenesisConsole::SaveBattery()
{
	if(_memoryManager) {
		_memoryManager->SaveBattery();
	}
}

BaseControlManager* GenesisConsole::GetControlManager()
{
	return _controlManager.get();
}

vector<CpuType> GenesisConsole::GetCpuTypes()
{
	if(_m68k) {
		return { CpuType::GenesisM68K, CpuType::GenesisZ80 };
	}
	return {};
}

double GenesisConsole::GetFps()
{
	return _genesisRegion == GenesisRegion::Pal ? 49.701 : 59.923;
}

PpuFrameInfo GenesisConsole::GetPpuFrame()
{
	PpuFrameInfo frame{};
	if(_vdp) {
		frame.FrameBuffer = (uint8_t*)_vdp->GetFramebuffer();
		frame.Width = _vdp->GetScreenWidth();
		frame.Height = _vdp->GetScreenHeight();
		frame.FrameBufferSize = GenesisScreenWidthH40 * GenesisScreenHeightPal * sizeof(uint32_t);
		frame.FrameCount = _vdp->GetFrameCount();
		frame.ScanlineCount = frame.Height;
		frame.FirstScanline = 0;
	} else {
		static vector<uint32_t> blackFrame;
		if(blackFrame.empty()) blackFrame.resize(GenesisScreenWidthH40 * GenesisScreenHeightNtsc, 0);
		frame.FrameBuffer = (uint8_t*)blackFrame.data();
		frame.Width = GenesisScreenWidthH40;
		frame.Height = GenesisScreenHeightNtsc;
		frame.FrameBufferSize = blackFrame.size() * sizeof(uint32_t);
		frame.FrameCount = _frameCount;
		frame.ScanlineCount = frame.Height;
		frame.FirstScanline = 0;
	}
	return frame;
}

BaseVideoFilter* GenesisConsole::GetVideoFilter(bool getDefaultFilter)
{
	return new GenesisDefaultVideoFilter(_emu, this);
}

uint64_t GenesisConsole::GetMasterClock()
{
	return _masterClock;
}

uint32_t GenesisConsole::GetMasterClockRate()
{
	return (_genesisRegion == GenesisRegion::Pal)
		? GenesisMasterClockPal
		: GenesisMasterClockNtsc;
}

AddressInfo GenesisConsole::GetAbsoluteAddress(AddressInfo& relAddress)
{
	if(_memoryManager) {
		return _memoryManager->GetAbsoluteAddress((uint32_t)relAddress.Address, CpuType::GenesisM68K);
	}
	return { -1, MemoryType::None };
}

AddressInfo GenesisConsole::GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType)
{
	if(_memoryManager) {
		return _memoryManager->GetRelativeAddress(absAddress, cpuType);
	}
	return { -1, MemoryType::None };
}

void GenesisConsole::GetConsoleState(BaseState& state, ConsoleType consoleType)
{
	auto& gs = (GenesisState&)state;
	gs.FrameCount = _frameCount;
	gs.Region = _genesisRegion;

	if(_m68k) {
		for(int i = 0; i < 8; i++) {
			gs.M68K.D[i] = _m68k->GetDataReg(i);
			gs.M68K.A[i] = _m68k->GetAddrReg(i);
		}
		gs.M68K.PC = _m68k->GetPC();
		gs.M68K.SR = _m68k->GetSR();
		gs.M68K.SSP = _m68k->GetAddrReg(7);
		gs.M68K.Stopped = _m68k->IsStopped();
	}

	if(_z80) {
		gs.Z80.A = _z80->GetA();
		gs.Z80.Flags = _z80->GetFlags();
		gs.Z80.B = _z80->GetB();
		gs.Z80.C = _z80->GetC();
		gs.Z80.D = _z80->GetD();
		gs.Z80.E = _z80->GetE();
		gs.Z80.H = _z80->GetH();
		gs.Z80.L = _z80->GetL();
		gs.Z80.IX = _z80->GetIX();
		gs.Z80.IY = _z80->GetIY();
		gs.Z80.SP = _z80->GetSP();
		gs.Z80.PC = _z80->GetPC();
		gs.Z80.I = _z80->GetI();
		gs.Z80.R = _z80->GetR();
		gs.Z80.Halted = _z80->IsHalted();
	}

	if(_vdp) {
		gs.Vdp.VCounter = _vdp->GetVCounter();
		gs.Vdp.HCounter = _vdp->GetHCounter();
		gs.Vdp.VBlank = _vdp->IsVblank();
		gs.Vdp.HBlank = _vdp->IsHblank();
		gs.Vdp.DisplayEnable = _vdp->IsDisplayEnable();
		for(uint32_t i = 0; i < 64; i++) {
			gs.Vdp.Cram[i] = _vdp->DebugReadCRAM(i);
		}
	}
}

void GenesisConsole::UpdateRegion()
{
	GenesisConfig& cfg = _emu->GetSettings()->GetGenesisConfig();
	switch(cfg.Region) {
		case ConsoleRegion::Auto:
			//Parse ROM header region string at offset $1F0.
			//If 'E' is present but 'U' and 'J' are not, select PAL.
			//Otherwise default to NTSC.
			if(_romRegion.find('E') != string::npos &&
			   _romRegion.find('U') == string::npos &&
			   _romRegion.find('J') == string::npos) {
				_region = ConsoleRegion::Pal;
				_genesisRegion = GenesisRegion::Pal;
			} else {
				_region = ConsoleRegion::Ntsc;
				_genesisRegion = GenesisRegion::Ntsc;
			}
			break;
		case ConsoleRegion::Ntsc:
			_region = ConsoleRegion::Ntsc;
			_genesisRegion = GenesisRegion::Ntsc;
			break;
		case ConsoleRegion::Pal:
			_region = ConsoleRegion::Pal;
			_genesisRegion = GenesisRegion::Pal;
			break;
		default:
			_region = ConsoleRegion::Ntsc;
			_genesisRegion = GenesisRegion::Ntsc;
			break;
	}
	_model = cfg.Model;

	if(_psg) _psg->SetRegion(_region);
	if(_ym2612) _ym2612->SetRegion(_region);
}

void GenesisConsole::Serialize(Serializer& s)
{
	SV(_m68k);
	SV(_z80);
	SV(_vdp);
	SV(_memoryManager);
	SV(_controlManager);
	SV(_psg);
	SV(_ym2612);

	SV(_frameCount);
	SV(_masterClock);
	SV(_sramEnable);
	SV(_sramWritable);
	SV(_banked);
	for(int i = 0; i < 8; i++) {
		SVI(_romBank[i]);
	}
}
