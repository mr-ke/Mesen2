#include "pch.h"
#include "Genesis/Mcd/GenesisMcd.h"
#include "Genesis/GenesisM68K.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/FirmwareHelper.h"
#include "Shared/MessageManager.h"
#include "Shared/CdReader.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/Serializer.h"
#include "Utilities/VirtualFile.h"

//--- bit helpers (mirror ares n16 .bit(n) / .bit(lo,hi) / .byte(0,1)) ---
static inline bool bit(uint32_t v, int b) { return (v >> b) & 1; }
static inline uint32_t bits(uint32_t v, int lo, int hi) { return (v >> lo) & ((1u << (hi - lo + 1)) - 1u); }
static inline uint16_t setbit(uint16_t v, int b, bool x) { return (uint16_t)((v & ~(1u << b)) | (x ? (1u << b) : 0)); }
static inline uint16_t setbits(uint16_t v, int lo, int hi, uint32_t x) {
	uint32_t m = ((1u << (hi - lo + 1)) - 1u) << lo;
	return (uint16_t)((v & ~m) | ((x << lo) & m));
}

GenesisMcd::GenesisMcd()
{
	_bios.resize(128 * 1024 / 2, 0);
	_pram.resize(512 * 1024 / 2, 0);
	_wram.resize(256 * 1024 / 2, 0);
	_bram.resize(8 * 1024, 0);
	//CDC RAM (16 KB) is owned by _cdc (matching ares cdc.ram); allocated in
	//GenesisMcdCdc::Power(). PCM RAM (64 KB) lives here for Phase D.
	_pcmRam.resize(64 * 1024, 0);

	_subM68k = unique_ptr<GenesisM68K>(new GenesisM68K());
	//The sub-CPU reuses the M68K interpreter. It runs with _emu == nullptr for
	//now (debugger attribution / a dedicated sub-CPU CpuType is Phase F), so
	//the ProcessInstruction/ProcessInterrupt instrumentation calls are skipped.

	//Wire subsystem back-pointers so CDC/CDD/Timer can reach the disc, raise
	//IRQs, and route DMA writes through this class. Set once here (idempotent).
	_cdc.SetMcd(this);
	_cdd.SetMcd(this);
	_timer.SetMcd(this);
}

GenesisMcd::~GenesisMcd() = default;

bool GenesisMcd::LoadBios()
{
	//Load a 128KB Mega CD / Sega CD BIOS into _bios (big-endian word array).
	//Region-appropriate filenames are tried first, then any region as a
	//fallback (matches the BIOS region being best-effort for Phase A).
	auto loadFromFile = [&](VirtualFile& f) -> bool {
		if(f.IsValid() && f.GetSize() >= 0x20000) {
			vector<uint8_t> data;
			f.ReadFile(data);
			_bios.resize(0x20000 / 2);
			for(size_t i = 0; i < _bios.size(); i++) {
				_bios[i] = (uint16_t)((data[2 * i] << 8) | data[2 * i + 1]);
			}
			return true;
		}
		return false;
	};

	auto tryLoad = [&](const string& filename) -> bool {
		VirtualFile f(FolderUtilities::CombinePath(FolderUtilities::GetFirmwareFolder(), filename));
		return loadFromFile(f);
	};

	//Check for an explicit BIOS path override from config (MegaCdBiosPath).
	//When set, this takes priority over the firmware-folder lookup.
	if(_emu) {
		const char* overridePath = _emu->GetSettings()->GetGenesisConfig().MegaCdBiosPath;
		if(overridePath && overridePath[0] != '\0') {
			VirtualFile f(overridePath);
			if(loadFromFile(f)) {
				return true;
			}
		}
	}

	auto tryAll = [&]() -> bool {
		if(_region == ConsoleRegion::NtscJapan) {
			if(tryLoad("bios_CD_J.bin") || tryLoad("[BIOS] Mega CD (Japan).bin") || tryLoad("segacd_jp.bin")) return true;
		} else if(_region == ConsoleRegion::Pal) {
			if(tryLoad("bios_CD_E.bin") || tryLoad("[BIOS] Mega CD (Europe).bin") || tryLoad("segacd_eu.bin")) return true;
		} else {
			if(tryLoad("bios_CD_U.bin") || tryLoad("[BIOS] Sega CD (USA).bin") || tryLoad("segacd_us.bin")) return true;
		}
		//Last resort: accept any region's BIOS.
		return tryLoad("bios_CD_U.bin") || tryLoad("bios_CD_J.bin") || tryLoad("bios_CD_E.bin");
	};

	if(tryAll()) {
		return true;
	}

	//BIOS not present in the firmware folder — ask the UI to prompt the user,
	//using the same MissingFirmware flow as PCE/SNES/etc. The UI validates the
	//selection is 128KB and copies it into the firmware folder as bios_CD_U.bin,
	//after which the retry below picks it up via the any-region fallback.
	//
	//This is the correct "load the Mega CD BIOS" prompt. Previously the .cue
	//was misrouted to the PC Engine core (tried before Genesis in TryLoadRom),
	//whose LoadFirmware() matched the ISO9660 PVD signature at sector 0x10
	//(present on every Sega CD data disc) and prompted for a 32KB PCE Games
	//Express card instead. PceConsole now bails out on Mega-CD discs.
	if(_emu) {
		MissingFirmwareMessage msg("bios_CD_U.bin", FirmwareType::MegaCd, 0x20000);
		_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::MissingFirmware, &msg);
		if(tryAll()) {
			return true;
		}
	}

	MessageManager::DisplayMessage("Error", "Could not find Mega CD BIOS (128KB) in firmware folder. Expected e.g. bios_CD_U.bin / bios_CD_J.bin / bios_CD_E.bin");
	return false;
}

void GenesisMcd::WireSubCpuCallbacks()
{
	//Wire the sub-CPU bus to the MCD internal bus. Address arrives word-aligned
	//(the GenesisM68K core passes address & ~1 with upper/lower byte selects).
	_subM68k->BusRead = [this](uint8_t upper, uint8_t lower, uint32_t address) -> uint16_t {
		return ReadInternal(upper, lower, address);
	};
	_subM68k->BusWrite = [this](uint8_t upper, uint8_t lower, uint32_t address, uint16_t data) {
		WriteInternal(upper, lower, address, data);
	};
	_subM68k->BusIdle = [this](uint32_t cycles) { _subM68k->AddCycles(cycles); };
	_subM68k->BusWait = [this](uint32_t cycles) { _subM68k->AddCycles(cycles); };
	_subM68k->CheckInterrupts = [this]() -> bool { return CheckSubCpuInterrupts(); };
}

void GenesisMcd::Power(bool reset)
{
	WireSubCpuCallbacks();

	if(!reset) _irq = GenesisMcdIrq{};
	ResetCpu();

	uint32_t vec4 = _io.vectorLevel4;
	_io = McdIo{};
	_io.vectorLevel4 = reset ? vec4 : (uint32_t)~0u;
	_counter = McdCounter{};
	_led = McdLed{};
	_communication = McdCommunication{};

	//Power the Phase B subsystems. CDC/CDD/Timer each hold a back-pointer to
	//this (set in the constructor); they reach the disc via HasDisc()/
	//ReadRawSector() and raise IRQs via RaiseCdcIrq()/RaiseCddIrq()/
	//RaiseTimerIrq(). _cdd.Power→Insert needs _disc set first (SetDisc is
	//called before Power in GenesisConsole::LoadSegaCd).
	_cdc.Power(reset);
	_cdd.Power(reset);
	_timer.Power(reset);
}

void GenesisMcd::ResetCpu()
{
	//ares resetCpu: M68000::power() reloads the reset vector from the internal
	//bus (PRAM[0..6]), then the reset IRQ is raised (serviced in main()).
	_subM68k->Power();
	_irq.reset.enable = true;
	_irq.Raise(_irq.reset);
}

void GenesisMcd::Step(uint32_t clocks)
{
	//ares MCD::step. Drives every sub-CPU peripheral through dividers:
	//  divider/384 → cdc.clock + cdd.clock + timer.clock   (one tick each)
	//  dma/6       → cdc.transfer.dma()                     (one DMA step)
	//  pcm/<rate>  → cdd.sample() (CD-DA; Phase D)
	_counter.divider += clocks;
	while(_counter.divider >= 384) {
		_counter.divider -= 384;
		_cdc.Clock();
		_cdd.Clock();
		_timer.Clock();
	}
	_counter.dma += clocks;
	while(_counter.dma >= 6) {
		_counter.dma -= 6;
		_cdc.TransferDma();
	}
	//CD-DA sample rate (Phase D): _counter.pcm accumulates and feeds cdd.Sample().
	(void)clocks;
}

// ============================================================================
// Disc bridge — feeds raw 2352-byte sectors from DiscInfo to the CDC decoder.
// ares seeks the disc file at (leadIn + sector) * 2448 and readm()s the raw
// sector. Mesen2's DiscInfo stores sectors without lead-in (Tracks[0].
// FirstSector == 0), so the byte offset is simply FileOffset + LBA*sectorSize.
//
// For Audio/Mode1_2352 tracks the raw 2352 bytes are read directly. For
// Mode1_2048 tracks (no sync/header in the file) the full 2352-byte sector is
// synthesized: 12-byte sync + 4-byte BCD MSF/mode header + 2048 user bytes +
// zeroed EDC/ECC. The CDC only validates via the HEAD registers (synthesized
// separately in Decode() from the same LBA), so zeroed EDC/ECC is fine for a
// perfect-disc HLE.
// ============================================================================
void GenesisMcd::ReadRawSector(uint32_t sector, uint8_t out[2352])
{
	if(!_disc) { memset(out, 0, 2352); return; }
	int32_t track = _disc->GetTrack(sector);
	if(track < 0) { memset(out, 0, 2352); return; }

	TrackInfo& trk = _disc->Tracks[track];
	uint32_t sectorSize = trk.GetSectorSize();
	uint32_t byteOffset = trk.FileOffset + (sector - trk.FirstSector) * sectorSize;

	if(sectorSize == 2352) {
		//Raw sector present in the image — read all 2352 bytes verbatim.
		VirtualFile& f = _disc->Files[trk.FileIndex];
		for(uint32_t i = 0; i < 2352; i++) {
			out[i] = f.ReadByte(byteOffset + i);
		}
		return;
	}

	//Mode1_2048 (or fallback): synthesize a Mode 1 raw sector.
	//Sync pattern: 00 FF×10 00.
	static const uint8_t sync[12] = {
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
	};
	memcpy(out, sync, 12);

	//Address field: absolute MSF in BCD (LBA convention, no lead-in — matches
	//the CDD's LbaToMsf so HEAD-register reads agree with CDD status reports).
	uint32_t m = sector / 75 / 60;
	uint32_t s = sector / 75 % 60;
	uint32_t fr = sector % 75;
	out[12] = CdReader::ToBcd((uint8_t)m);
	out[13] = CdReader::ToBcd((uint8_t)s);
	out[14] = CdReader::ToBcd((uint8_t)fr);
	out[15] = 0x01;  //Mode 1

	//User data (2048 bytes) from the image.
	VirtualFile& f = _disc->Files[trk.FileIndex];
	for(uint32_t i = 0; i < 2048; i++) {
		out[16 + i] = f.ReadByte(byteOffset + i);
	}

	//EDC (4) + intermediate (8) + ECC (276) — zeroed (perfect-disc HLE).
	memset(out + 2064, 0, 2352 - 2064);
}

// ============================================================================
// CDC DMA destination 4 (PCM RAM). ares splits each 16-bit CDC word into two
// 8-bit writes to the active 4 KB PCM bank (0x1000-0x1FFF):
//   pcm.write(0x1000 | n12(address >> 1) | 1, data.byte(1))  //high byte
//   pcm.write(0x1000 | n12(address >> 1) | 0, data.byte(0))  //low byte
// Phase B writes the bytes directly into _pcmRam; the PCM chip itself (Phase D)
// will own the audio rendering but shares this RAM.
// ============================================================================
void GenesisMcd::PcmDmaWrite(uint32_t address, uint16_t data)
{
	uint32_t base = 0x1000u | ((address >> 1) & 0x0FFFu);
	_pcmRam[(base | 1u) & 0xFFFFu] = (uint8_t)((data >> 8) & 0xFF);
	_pcmRam[(base | 0u) & 0xFFFFu] = (uint8_t)(data & 0xFF);
}

bool GenesisMcd::CheckSubCpuInterrupts()
{
	//ares MCD::main(): priority dispatch. `mask` is the sub-CPU SR interrupt
	//priority field (r.i); an interrupt is taken only when level > mask.
	uint8_t mask = _subM68k->GetInterruptMask();

	if(_irq.pending) {
		if(1 > mask && _irq.gpu.pending) { _irq.Lower(_irq.gpu); _subM68k->Interrupt(25, 1); return true; }       //VLevel1
		if(2 > mask && _irq.external.pending) { _irq.Lower(_irq.external); _subM68k->Interrupt(26, 2); return true; } //VLevel2
		if(3 > mask && _irq.timer.pending) { _irq.Lower(_irq.timer); _subM68k->Interrupt(27, 3); return true; }   //VLevel3
		if(4 > mask && _irq.cdd.pending) { _irq.Lower(_irq.cdd); _subM68k->Interrupt(28, 4); return true; }       //VLevel4
		if(5 > mask && _irq.cdc.pending) {
			_irq.Lower(_irq.cdc);
			_subM68k->Interrupt(29, 5);
			return true;
		}       //VLevel5
		if(6 > mask && _irq.subcode.pending) { _irq.Lower(_irq.subcode); _subM68k->Interrupt(30, 6); return true; }//VLevel6
		if(_irq.reset.pending) {
			//ares MCD::main() reset handler: re-read the sub-CPU reset vector
			//from PRAM ($0..$6) and refill the prefetch pipeline. The BIOS
			//writes the sub-CPU boot vector into PRAM *after* power-on, so the
			//vector must be re-read here, not just during Power(). This matches
			//the ares sequence: r.a[7]=read(0,2); r.pc=read(4,6); prefetch×2.
			_irq.Lower(_irq.reset);
			_subM68k->ReloadResetVector();
			return true;
		}
	}
	return false;
}

// ============================================================================
// Internal bus (sub-CPU) — ares/md/mcd/bus-internal.cpp
// ============================================================================

uint16_t GenesisMcd::ReadInternal(uint8_t upper, uint8_t lower, uint32_t address)
{
	uint16_t data = 0xFFFF; //open bus

	//The M68000 has a 24-bit external address bus. Absolute-short addressing
	//sign-extends the 16-bit displacement to 32 bits (e.g. ($8003).W →
	//0xFFFF8003), so without masking, IO-region reads at 0xFF80xx would be
	//passed as 0xFFFF80xx and miss every range check below — returning open
	//bus instead of the register value. The main CPU masks in M68KRead
	//(address &= 0x00FFFFFE); the sub-CPU must do the same here. Mirrors
	//ares, where addresses are n24 (inherently 24-bit).
	address &= 0x00FFFFFF;

	if(address >= 0x000000 && address <= 0x07FFFF) {
		return _pram[(address >> 1) & 0x3FFFF];
	}

	if(address >= 0x080000 && address <= 0x0BFFFF) {
		if(!_io.wramMode) {
			//2M mode: sub-CPU sees WRAM only when it owns it (wramSwitch == 1).
			if(!_io.wramSwitch) return data;
			return _wram[(address >> 1) & 0x1FFFF];
		} else {
			//1M dot-mapped window (Phase A: basic passthrough, no nibble pack).
			//TODO Phase C: port dot-mapping from bus-internal.cpp.
			uint32_t a = ((address >> 1) & ~1u) | (uint32_t)!_io.wramSelect;
			return _wram[a & 0x1FFFF];
		}
	}

	if(address >= 0x0C0000 && address <= 0x0DFFFF) {
		if(_io.wramMode) {
			uint32_t a = (address & ~1u) | (uint32_t)!_io.wramSelect;
			return _wram[a & 0x1FFFF];
		}
		return data;
	}

	if(address >= 0xFE0000 && address <= 0xFEFFFF) {
		if(!lower) return data;
		return (uint16_t)_bram[(address >> 1) & 0x1FFF];
	}

	if(address >= 0xFF0000 && address <= 0xFF7FFF) {
		//PCM RAM (Phase D). Phase A: open bus.
		if(!lower) return data;
		return data;
	}

	if(address >= 0xFF8000 && address <= 0xFFFFFF) {
		return ReadIO(upper, lower, address, data);
	}

	return data;
}

void GenesisMcd::WriteInternal(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data)
{
	//24-bit address bus mask — see ReadInternal for rationale. Without this,
	//sign-extended addresses from absolute-short addressing (e.g. BSET on
	//(0xFF8003)) would miss the IO range checks and silently drop the write.
	address &= 0x00FFFFFF;

	if(address >= 0x000000 && address <= 0x07FFFF
	&& address >= (uint32_t)_io.pramProtect << 9) {
		uint32_t idx = (address >> 1) & 0x3FFFF;
		if(upper) _pram[idx] = (_pram[idx] & 0x00FF) | (data & 0xFF00);
		if(lower) _pram[idx] = (_pram[idx] & 0xFF00) | (data & 0x00FF);
		return;
	}

	if(address >= 0x080000 && address <= 0x0BFFFF) {
		if(!_io.wramMode) {
			//2M: sub-CPU can only write when it owns WRAM. ares busy-waits
			//(DTACK) until ownership; the single-threaded model can't, so we
			//drop the write when not owned. TODO Phase C: revisit stalling.
			if(!_io.wramSwitch) return;
			uint32_t idx = (address >> 1) & 0x1FFFF;
			if(upper) _wram[idx] = (_wram[idx] & 0x00FF) | (data & 0xFF00);
			if(lower) _wram[idx] = (_wram[idx] & 0xFF00) | (data & 0x00FF);
		} else {
			//1M dot-mapped write (Phase A: basic passthrough).
			uint32_t a = ((address >> 1) & ~1u) | (uint32_t)!_io.wramSelect;
			uint32_t idx = a & 0x1FFFF;
			if(upper) _wram[idx] = (_wram[idx] & 0x00FF) | (data & 0xFF00);
			if(lower) _wram[idx] = (_wram[idx] & 0xFF00) | (data & 0x00FF);
		}
		return;
	}

	if(address >= 0x0C0000 && address <= 0x0DFFFF) {
		if(_io.wramMode) {
			uint32_t a = (address & ~1u) | (uint32_t)!_io.wramSelect;
			uint32_t idx = a & 0x1FFFF;
			if(upper) _wram[idx] = (_wram[idx] & 0x00FF) | (data & 0xFF00);
			if(lower) _wram[idx] = (_wram[idx] & 0xFF00) | (data & 0x00FF);
		}
		return;
	}

	if(address >= 0xFE0000 && address <= 0xFEFFFF) {
		if(!lower) return;
		_bram[(address >> 1) & 0x1FFF] = data & 0xFF;
		return;
	}

	if(address >= 0xFF0000 && address <= 0xFF7FFF) {
		//PCM RAM (Phase D). Phase A: ignore.
		return;
	}

	if(address >= 0xFF8000 && address <= 0xFFFFFF) {
		WriteIO(upper, lower, address, data);
		return;
	}
}

// ============================================================================
// External bus (main CPU) — ares/md/mcd/bus-external.cpp
// ============================================================================

uint16_t GenesisMcd::ReadExternal(uint8_t upper, uint8_t lower, uint32_t address)
{
	uint16_t data = 0xFFFF;
	address &= ~0x1C0000u; //mirrors: clear bits 18-20

	if(address >= 0x000000 && address <= 0x01FFFF) {
		if(address == 0x70) {
			return (uint16_t)(_io.vectorLevel4 >> 16);
		}
		if(address == 0x72) {
			return (uint16_t)(_io.vectorLevel4 >> 0);
		}
		return _bios[(address >> 1) & 0xFFFF];
	}

	if(address >= 0x020000 && address <= 0x03FFFF) {
		if(!_io.request) return data;
		uint32_t a = ((uint32_t)_io.pramBank << 17) | (address & 0x1FFFF);
		return _pram[(a >> 1) & 0x3FFFF];
	}

	if(address >= 0x200000 && address <= 0x23FFFF) {
		if(!_io.wramMode) {
			//2M: main CPU sees WRAM only when it owns it (wramSwitch == 0).
			if(_io.wramSwitch) return data;
			uint32_t a = (address >> 1) & 0x1FFFF;
			//TODO Phase C: VDP-DMA wramLatch one-access delay.
			return _wram[a];
		} else {
			if(address >= 0x220000) {
				address = CellMapAddress(address);
			}
			uint32_t a = (address & ~1u) | (uint32_t)_io.wramSelect;
			return _wram[a & 0x1FFFF];
		}
	}

	return data;
}

void GenesisMcd::WriteExternal(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data)
{
	address &= ~0x1C0000u;

	if(address >= 0x020000 && address <= 0x03FFFF) {
		if(!_io.request) return;
		uint32_t a = ((uint32_t)_io.pramBank << 17) | (address & 0x1FFFF);
		uint32_t idx = (a >> 1) & 0x3FFFF;
		if(upper) _pram[idx] = (_pram[idx] & 0x00FF) | (data & 0xFF00);
		if(lower) _pram[idx] = (_pram[idx] & 0xFF00) | (data & 0x00FF);
		return;
	}

	if(address >= 0x200000 && address <= 0x23FFFF) {
		if(!_io.wramMode) {
			if(_io.wramSwitch) return;
			uint32_t idx = (address >> 1) & 0x1FFFF;
			if(upper) _wram[idx] = (_wram[idx] & 0x00FF) | (data & 0xFF00);
			if(lower) _wram[idx] = (_wram[idx] & 0xFF00) | (data & 0x00FF);
		} else {
			if(address >= 0x220000) {
				address = CellMapAddress(address);
			}
			uint32_t a = (address & ~1u) | (uint32_t)_io.wramSelect;
			uint32_t idx = a & 0x1FFFF;
			if(upper) _wram[idx] = (_wram[idx] & 0x00FF) | (data & 0xFF00);
			if(lower) _wram[idx] = (_wram[idx] & 0xFF00) | (data & 0x00FF);
		}
		return;
	}
}

uint32_t GenesisMcd::CellMapAddress(uint32_t address)
{
	//1M cell-mapped window address translation (bus-external.cpp 0x220000+).
	if(address < 0x230000) {
		//V32
		return ((address & 0x0FC00) >> 8) | ((address & 0x003FC) << 6) | (address & 0x10002);
	} else if(address < 0x238000) {
		//V16
		return ((address & 0x07E00) >> 7) | ((address & 0x001FC) << 6) | (address & 0x18002);
	} else if(address < 0x23C000) {
		//V8
		return ((address & 0x03F00) >> 6) | ((address & 0x000FC) << 6) | (address & 0x1C002);
	} else {
		//V4
		return ((address & 0x01F80) >> 5) | ((address & 0x0007C) << 6) | (address & 0x1E002);
	}
}

// ============================================================================
// External gate-array IO (main CPU 0xA12000-0xA1203F) — ares/md/mcd/io-external.cpp
// ============================================================================

uint16_t GenesisMcd::ReadExternalIO(uint8_t upper, uint8_t lower, uint32_t address)
{
	uint16_t data = 0xFFFF;
	address &= ~0xC0u; //a12040-a120ff mirrors a12000-a1203f (bits 6,7)

	if(address == 0xA12000) {
		data = setbit(data, 0, _io.run);
		data = setbit(data, 1, _io.request);
		data &= ~0x00FC;                              //bits 2-7 unmapped
		data = setbit(data, 8, _irq.external.pending);
		data &= ~0x7F00;                              //bits 9-14 unmapped
		data = setbit(data, 15, _irq.external.enable);
	}

	if(address == 0xA12002) {
		data = setbit(data, 0, !_io.wramMode ? !_io.wramSwitch : (bool)_io.wramSelect);
		data = setbit(data, 1, !_io.wramMode ? _io.wramSwitch : _io.wramSwitchRequest);
		data = setbit(data, 2, _io.wramMode);
		data &= ~0x0038;                              //bits 3-5 unmapped
		data = setbits(data, 6, 7, _io.pramBank);
		data = setbits(data, 8, 15, _io.pramProtect);
	}

	if(address == 0xA12004) {
		//CDC transfer status (ares io-external.cpp 0xa12004).
		data &= 0xF800;                              //bits 0-7, 11-13 unmapped
		data = setbits(data, 8, 10, _cdc.GetTransferDestination());
		data = setbit(data, 14, _cdc.GetTransferReady());
		data = setbit(data, 15, _cdc.GetTransferCompleted());
	}

	if(address == 0xA12006) {
		data = (uint16_t)_io.vectorLevel4;
	}

	if(address == 0xA1200C) {
		//CDC stopwatch (12-bit), readable from the main CPU (ares io-external).
		data = setbits(data, 0, 11, _cdc.GetStopwatch());
		data &= 0x0FFF;                              //bits 12-15 unmapped
	}

	if(address == 0xA1200E) {
		data = (uint16_t)((_communication.cfm << 8) | _communication.cfs);
	}

	if(address >= 0xA12010 && address <= 0xA1201F) {
		uint32_t index = (address - 0xA12010) >> 1;
		data = _communication.command[index];
	}

	if(address >= 0xA12020 && address <= 0xA1202F) {
		uint32_t index = (address - 0xA12020) >> 1;
		data = _communication.status[index];
	}

	return data;
}

void GenesisMcd::WriteExternalIO(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data)
{
	address &= ~0xC0u;

	if(address == 0xA12000) {
		if(lower) {
			if(_io.run && !bit(data, 0)) {
				//run -> stop transition resets the sub-CPU.
				ResetCpu();
			}
			_io.run = bit(data, 0);
			_io.request = _io.run ? bit(data, 1) : true; //asserting reset forces busreq
			_io.halt = _io.request;
		}
		if(upper) {
			if(bit(data, 8)) _irq.Raise(_irq.external);
		}
	}

	if(address == 0xA12002) {
		if(lower) {
			if(!bit(data, 1)) _io.wramSwitchRequest = true;
			if(bit(data, 1)) _io.wramSwitch = true;
			_io.pramBank = (uint8_t)bits(data, 6, 7);
		}
		if(upper) {
			_io.pramProtect = (uint8_t)bits(data, 8, 15);
		}
	}

	if(address == 0xA12006) {
		if(upper) _io.vectorLevel4 = (_io.vectorLevel4 & 0x00FF) | ((data & 0xFF00) << 0);
		if(lower) _io.vectorLevel4 = (_io.vectorLevel4 & 0xFF00) | (data & 0x00FF);
		//Note: ares stores vectorLevel4 as a 32-bit value assembled from the two
		//bytes written here (0xA12006 hi/lo). The high word is set via $000070/72
		//reads; here only the low 16 bits are writable from the main CPU side.
		_io.vectorLevel4 = (_io.vectorLevel4 & 0xFFFF0000u) | (data & 0xFFFF);
	}

	if(address == 0xA1200E) {
		//8-bit register: all writes go to the high byte (cfm).
		_communication.cfm = (uint8_t)((data >> 8) & 0xFF);
	}

	if(address >= 0xA12010 && address <= 0xA1201F) {
		uint32_t index = (address - 0xA12010) >> 1;
		if(lower) _communication.command[index] = (_communication.command[index] & 0xFF00) | (data & 0x00FF);
		if(upper) _communication.command[index] = (_communication.command[index] & 0x00FF) | (data & 0xFF00);
	}

	//0xA12020-0xA1202F (status) is read-only from the main CPU.
}

// ============================================================================
// Internal gate-array IO (sub-CPU 0xFF8000+) — ares/md/mcd/io-internal.cpp
// ============================================================================

uint16_t GenesisMcd::ReadIO(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data)
{
	address = 0xFF8000 | (address & 0x1FF);

	if(address == 0xFF8000) {
		data = setbit(data, 0, true);       //peripheral ready
		data &= ~0x000E;                    //bits 1-3 unmapped
		data &= ~0x00F0;                    //bits 4-7 version# = 0
		data = setbit(data, 8, _led.red);
		data = setbit(data, 9, _led.green);
		data &= ~0xFC00;                    //bits 10-15 unmapped
	}

	if(address == 0xFF8002) {
		data = setbit(data, 0, !_io.wramMode ? !_io.wramSwitch : (bool)_io.wramSelect);
		data = setbit(data, 1, !_io.wramMode ? _io.wramSwitch : _io.wramSwitchRequest);
		data = setbit(data, 2, _io.wramMode);
		data = setbits(data, 3, 4, _io.wramPriority);
		data &= ~0x00E0;                    //bits 5-7 unmapped
		data = setbits(data, 8, 15, _io.pramProtect);
	}

	if(address == 0xFF8004) {
		//CDC: lo = register address (5 bits), hi = transfer destination (3 bits).
		data = setbits(data, 0, 4, _cdc.GetAddress());
		data &= ~0x00E0;                    //bits 5-7 unmapped
		data = setbits(data, 8, 10, _cdc.GetTransferDestination());
		data &= ~0x3800;                    //bits 11-13 unmapped
		data = setbit(data, 14, _cdc.GetTransferReady());
		data = setbit(data, 15, _cdc.GetTransferCompleted());
	}

	if(address == 0xFF8006) {
		//CDC register read (lo byte). ares notes the byte select is unconfirmed;
		//the read auto-increments the CDC address (except COMIN at 0).
		if(lower) {
			data = (data & 0xFF00) | _cdc.Read();
		}
	}

	if(address == 0xFF8008) {
		//CDC transfer data read (main-CPU destination, full word).
		data = _cdc.TransferRead();
	}

	if(address == 0xFF800A) {
		//CDC transfer address (bits 3-18 of the address → data bits 0-15).
		data = _cdc.GetTransferAddress();
	}

	if(address == 0xFF800C) {
		//CDC stopwatch (12-bit free-running counter).
		data = setbits(data, 0, 11, _cdc.GetStopwatch());
		data &= ~0xF000;                    //bits 12-15 unmapped
	}

	if(address == 0xFF800E) {
		data = (uint16_t)((_communication.cfm << 8) | _communication.cfs);
	}

	if(address >= 0xFF8010 && address <= 0xFF801F) {
		uint32_t index = (address - 0xFF8010) >> 1;
		data = _communication.command[index];
	}

	if(address >= 0xFF8020 && address <= 0xFF802F) {
		uint32_t index = (address - 0xFF8020) >> 1;
		data = _communication.status[index];
	}

	if(address == 0xFF8030) {
		//Timer frequency (8-bit, low byte; high byte unmapped).
		data = (data & 0xFF00) | _timer.GetFrequency();
		data &= ~0xFF00;
	}

	if(address == 0xFF8032) {
		data &= ~0x0001;                    //bit 0 unmapped
		data = setbit(data, 1, _irq.gpu.enable);
		data = setbit(data, 2, _irq.external.enable);
		data = setbit(data, 3, _irq.timer.enable);
		data = setbit(data, 4, _irq.cdd.enable);
		data = setbit(data, 5, _irq.cdc.enable);
		data = setbit(data, 6, _irq.subcode.enable);
		data &= ~0xFF00;                    //bits 7-15 unmapped
	}

	if(address == 0xFF8034) {
		//CDD DAC is WRITE-only. Reads return open bus for bits 0-14 and 0 for
		//bit 15 (end-of-fade-data-transfer flag). ares io-internal 0xff8034.
		data &= 0x7FFF;
	}

	if(address == 0xFF8036) {
		//CDD control/status: bit 2 = hostClockEnable, bit 8 = current track is data.
		data = _cdd.ReadControl();
	}

	if(address >= 0xFF8038 && address <= 0xFF8041) {
		//CDD status nibbles (read-only). 10 nibbles packed 2 per word:
		//lo byte bits 0-3 = status[index|1], hi byte bits 8-11 = status[index|0].
		uint32_t index = address - 0xFF8038;
		data = _cdd.ReadStatus(index);
	}

	if(address >= 0xFF8042 && address <= 0xFF804B) {
		//CDD command nibbles (read-back of what the sub-CPU wrote).
		uint32_t index = address - 0xFF8042;
		data = 0;
		data |= (uint16_t)(_cdd.ReadCommandNibble(index | 1) & 0x0F);
		data |= (uint16_t)(_cdd.ReadCommandNibble(index | 0) & 0x0F) << 8;
	}

	if(address == 0xFF8068) {
		//CDD subcode position (Phase B: 0).
		data = _cdd.ReadSubcodePosition();
	}

	if(address >= 0xFF8100 && address <= 0xFF81FF) {
		//CDD subcode data (Phase B: 0).
		uint32_t index = (address - 0xFF8100) >> 1;
		data = _cdd.ReadSubcodeData(index);
	}

	//GPU font/stamps/image registers (0xFF804C-0xFF8066) remain open bus until
	//Phase E.
	return data;
}

void GenesisMcd::WriteIO(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data)
{
	address = 0xFF8000 | (address & 0x1FF);

	if(address == 0xFF8000) {
		if(lower) {
			//bit 0 == 0: peripheral reset (Phase A: no-op, ~100ms in hw)
		}
		if(upper) {
			_led.red = bit(data, 8);
			_led.green = bit(data, 9);
		}
	}

	if(address == 0xFF8002) {
		//8-bit register: all writes go to the low byte.
		if(_io.wramSelect != bit(data, 0)) _io.wramSwitchRequest = false;
		_io.wramSelect = bit(data, 0);
		_io.wramMode = bit(data, 2);
		_io.wramPriority = (uint8_t)bits(data, 3, 4);
		if(_io.wramSelect) _io.wramSwitch = false;
	}

	if(address == 0xFF8004) {
		//CDC: lo = register address (5 bits), hi = transfer destination (3 bits).
		if(lower) _cdc.SetAddress((uint8_t)(data & 0x1F));
		if(upper) _cdc.SetTransferDestination((uint8_t)((data >> 8) & 0x07));
	}

	if(address == 0xFF8006) {
		//CDC register write (lo byte). ares notes the byte select is unconfirmed;
		//the write auto-increments the CDC address (except SBOUT at 0).
		if(lower) _cdc.Write((uint8_t)(data & 0xFF));
	}

	if(address == 0xFF800A) {
		//CDC transfer address (bits 3-18).
		_cdc.SetTransferAddress(data);
	}

	if(address == 0xFF800C) {
		//CDC stopwatch clear (writing any value clears it to 0).
		_cdc.ClearStopwatch();
	}

	if(address == 0xFF800E) {
		//8-bit register: all writes go to the low byte (cfs).
		_communication.cfs = (uint8_t)(data & 0xFF);
	}

	if(address >= 0xFF8020 && address <= 0xFF802F) {
		uint32_t index = (address - 0xFF8020) >> 1;
		if(lower) _communication.status[index] = (_communication.status[index] & 0xFF00) | (data & 0x00FF);
		if(upper) _communication.status[index] = (_communication.status[index] & 0x00FF) | (data & 0xFF00);
	}

	if(address == 0xFF8030) {
		//Timer: 8-bit register, writes go to the low byte.
		//ares: timer.counter = timer.frequency = data.byte(0).
		if(lower) _timer.SetFrequency((uint8_t)(data & 0xFF));
	}

	if(address == 0xFF8032) {
		if(lower) {
			_irq.gpu.enable = bit(data, 1);
			_irq.external.enable = bit(data, 2);
			_irq.timer.enable = bit(data, 3);
			_irq.cdd.enable = bit(data, 4);
			_irq.cdc.enable = bit(data, 5);
			_irq.subcode.enable = bit(data, 6);
		}
	}

	if(address == 0xFF8034) {
		//CDD DAC config (Phase D stores; Phase B just records state).
		_cdd.WriteDac(data);
	}

	if(address == 0xFF8036) {
		//CDD control: bit 2 = host clock enable. Enabling it raises the CDD IRQ.
		_cdd.WriteControl(lower, data);
	}

	if(address >= 0xFF8042 && address <= 0xFF804B) {
		//CDD command nibbles (write). Writing the last nibble (index|1 == 9)
		//triggers Process(), which prepares the status response.
		uint32_t index = address - 0xFF8042;
		_cdd.WriteCommand(index, lower, upper, data);
	}

	//GPU font/stamps/image register writes (0xFF804C-0xFF8066) are ignored
	//until Phase E.
}

// ============================================================================
// ISerializable
// ============================================================================

void GenesisMcd::Serialize(Serializer& s)
{
	//Sub-CPU registers (BIOS/PRAM/WRAM/BRAM are not serialized as code; BRAM is
	//battery-backed separately by the console).
	if(_subM68k) SV(*_subM68k);

	SV(_io.run);
	SV(_io.request);
	SV(_io.halt);
	SV(_io.wramLatch);
	SV(_io.wramMode);
	SV(_io.wramSwitchRequest);
	SV(_io.wramSwitch);
	SV(_io.wramSelect);
	SV(_io.wramPriority);
	SV(_io.pramBank);
	SV(_io.pramProtect);
	SV(_io.vectorLevel4);

	SV(_led.red);
	SV(_led.green);

	SV(_counter.divider);
	SV(_counter.dma);
	SV(_counter.pcm);

	SV(_irq);

	SV(_communication.cfm);
	SV(_communication.cfs);
	for(int i = 0; i < 8; i++) SVI(_communication.command[i]);
	for(int i = 0; i < 8; i++) SVI(_communication.status[i]);

	//PRAM/WRAM contents (CDC-RAM is serialized via _cdc, PCM-RAM via Phase D).
	for(int i = 0; i < (int)_pram.size(); i++) SVI(_pram[i]);
	for(int i = 0; i < (int)_wram.size(); i++) SVI(_wram[i]);

	//Phase B subsystems (each implements Serialize internally).
	SV(_cdc);
	SV(_cdd);
	SV(_timer);
}
