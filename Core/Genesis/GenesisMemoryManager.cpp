#include "pch.h"
#include "Genesis/GenesisMemoryManager.h"
#include "Genesis/GenesisConsole.h"
#include "Genesis/GenesisM68K.h"
#include "Genesis/GenesisZ80.h"
#include "Genesis/GenesisVdp.h"
#include "Genesis/GenesisPsg.h"
#include "Genesis/GenesisYm2612.h"
#include "Genesis/GenesisControlManager.h"
#include "Shared/Emulator.h"
#include "Shared/BatteryManager.h"
#include "Shared/EmuSettings.h"
#include "Shared/CheatManager.h"
#include "Utilities/Serializer.h"

#ifdef _WIN32
#include <windows.h>
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif
#define GENESIS_DBG(fmt, ...) do { char _dbg_buf[512]; snprintf(_dbg_buf, sizeof(_dbg_buf), "[GENESIS] " fmt "\n", ##__VA_ARGS__); OutputDebugStringA(_dbg_buf); } while(0)
#else
#define GENESIS_DBG(fmt, ...) fprintf(stderr, "[GENESIS] " fmt "\n", ##__VA_ARGS__)
#endif

GenesisMemoryManager::GenesisMemoryManager()
{
	_m68kRam = new uint8_t[M68KRamSize];
	_z80Ram = new uint8_t[Z80RamSize];
	memset(_m68kRam, 0, M68KRamSize);
	memset(_z80Ram, 0, Z80RamSize);
}

GenesisMemoryManager::~GenesisMemoryManager()
{
	delete[] _m68kRam;
	delete[] _z80Ram;
	delete[] _originalSram;
}

void GenesisMemoryManager::Init(Emulator* emu, GenesisConsole* console,
	GenesisM68K* m68k, GenesisZ80* z80, GenesisVdp* vdp,
	GenesisPsg* psg, GenesisYm2612* ym2612,
	GenesisControlManager* controlManager,
	uint8_t* rom, uint32_t romSize,
	uint8_t* sram, uint32_t sramSize,
	uint32_t sramStart, bool sramWritable)
{
	GENESIS_DBG("MemoryManager::Init enter: rom=%p romSize=%u sram=%p sramSize=%u sramStart=0x%06X",
		(void*)rom, romSize, (void*)sram, sramSize, sramStart);

	_emu = emu;
	_console = console;
	_m68k = m68k;
	_z80 = z80;
	_vdp = vdp;
	_psg = psg;
	_ym2612 = ym2612;
	_controlManager = controlManager;

	_rom = rom;
	_romSize = romSize;
	_sram = sram;
	_sramSize = sramSize;
	_sramStart = sramStart;
	_sramWritable = sramWritable;
	_sramEnable = (sramSize > 0);

	//Register memory regions with the emulator for debugger/cheat support
	_emu->RegisterMemory(MemoryType::GenesisM68KRam, _m68kRam, M68KRamSize);
	_emu->RegisterMemory(MemoryType::GenesisZ80Ram, _z80Ram, Z80RamSize);
	if(_rom) _emu->RegisterMemory(MemoryType::GenesisCartridgeRom, _rom, _romSize);
	if(_sram && _sramSize > 0) _emu->RegisterMemory(MemoryType::GenesisCartridgeRam, _sram, _sramSize);

	//Initialize RAM
	console->InitializeRam(_m68kRam, M68KRamSize);
	console->InitializeRam(_z80Ram, Z80RamSize);

	//Load battery (SRAM)
	LoadBattery();
	if(_sram && _sramSize > 0) {
		_originalSram = new uint8_t[_sramSize];
		memcpy(_originalSram, _sram, _sramSize);
	}

	//Wire M68K bus callbacks
	GENESIS_DBG("  Wiring M68K bus callbacks...");
	_m68k->BusRead = [this](uint8_t upper, uint8_t lower, uint32_t address) -> uint16_t {
		static uint32_t dbgBusReadCount = 0;
		//Log reads from I/O, VDP, Z80 space, and any odd-aligned reads
		//Increased limit to 2000 to capture main loop reads
		if(dbgBusReadCount < 2000 && (address >= 0xA00000 || upper == 0 || lower == 0)) {
			GENESIS_DBG("BusRead: addr=0x%08X upper=%d lower=%d", address, upper, lower);
			dbgBusReadCount++;
		}
		return M68KRead(upper, lower, address);
	};
	_m68k->BusWrite = [this](uint8_t upper, uint8_t lower, uint32_t address, uint16_t data) {
		M68KWrite(upper, lower, address, data);
	};
	_m68k->BusIdle = [this](uint32_t cycles) {
		_m68k->AddCycles(cycles);
	};
	_m68k->BusWait = [this](uint32_t cycles) {
		_m68k->AddCycles(cycles);
	};
	_m68k->CheckInterrupts = [this]() -> bool {
		//Fast path: if no VDP interrupts are pending, skip the full check.
		//This is the common case (most instructions don't coincide with VBlank).
		if(!_vdp->GetVblankIrq() && !_vdp->GetHblankIrq() && !_vdp->GetExternalIrq())
			return false;
		//Debug: log when VDP interrupts are detected as pending
		static uint32_t dbgIntCheckCount = 0;
		if(dbgIntCheckCount < 200) {
			GENESIS_DBG("CheckInterrupts: vblank=%d hblank=%d ext=%d ipl=%u PC=0x%08X SR=0x%04X",
				_vdp->GetVblankIrq(), _vdp->GetHblankIrq(), _vdp->GetExternalIrq(),
				_m68k->GetInterruptMask(), _m68k->GetPC(), _m68k->GetSR());
			dbgIntCheckCount++;
		}
		return PollM68KInterruptsBool();
	};

	//Wire Z80 bus callbacks
	GENESIS_DBG("  Wiring Z80 bus callbacks...");
	_z80->BusRead = [this](uint16_t addr) -> uint8_t {
		return Z80Read(addr);
	};
	_z80->BusWrite = [this](uint16_t addr, uint8_t data) {
		Z80Write(addr, data);
	};
	_z80->BusWait = [this](uint32_t cycles) {
		_z80->AddCycles(cycles);
	};

	//Wire VDP DMA read callback
	GENESIS_DBG("  Wiring VDP DMA callback...");
	_vdp->DmaRead = [this](uint32_t address) -> uint16_t {
		return DmaRead(address);
	};

	GENESIS_DBG("MemoryManager::Init done");
}

void GenesisMemoryManager::Reset()
{
	_z80Bank = 0;
	_busreqLine = false;
	_busreqAck = false;
	_resetLine = false;
	_vdpEnable[0] = !_tmssEnable;
	_vdpEnable[1] = !_tmssEnable;
	_romEnable = !_tmssEnable;
	_io = {};
	_io.version = _tmssEnable ? 1 : 0;
}

// ============================================================================
// M68K bus read
// ============================================================================

uint16_t GenesisMemoryManager::M68KRead(uint8_t upper, uint8_t lower, uint32_t address)
{
	address &= 0x00FFFFFE; //word-align

	//0x000000-0x3FFFFF: Cartridge ROM
	if(address < 0x400000) {
		if(!_romEnable && _tmssEnable) {
			//TMSS: when ROM is disabled, reads go to TMSS BIOS (not implemented;
			//just return open bus for now)
			return 0xFFFF;
		}
		//Banked cartridge: use bank mapping
		if(address >= _sramStart && address < _sramStart + _sramSize && _sram && _sramEnable) {
			return ReadSramWord(address);
		}
		return ReadRomWord(address);
	}

	//0xA00000-0xA0FFFF: Z80 bus window
	if(address >= 0xA00000 && address <= 0xA0FFFF) {
		if(!_busreqAck && !_resetLine) {
			//Z80 bus not granted to M68K and Z80 not in reset — return open bus
			return 0xFFFF;
		}
		//Access Z80 RAM at 0xA00000-0xA01FFF (8KB mirrored)
		if(address <= 0xA01FFF) {
			uint32_t offset = address & 0x1FFF;
			uint16_t data;
			if(upper) data = _z80Ram[offset] << 8;
			else data = 0xFF00;
			if(lower) data = (_z80Ram[offset] & 0xFF) | (data & 0xFF00);
			else data |= 0x00FF;
			return data;
		}
		//0xA04000-0xA040FF: YM2612
		if(address >= 0xA04000 && address <= 0xA040FF) {
			//YM2612 status read — only lower byte valid
			uint8_t status = _ym2612->ReadStatus();
			uint16_t data = 0xFF00 | status;
			return data;
		}
		return 0xFFFF;
	}

	//0xA10000-0xA1FFFF: I/O region
	if(address >= 0xA10000 && address <= 0xA1FFFF) {
		uint16_t result = ReadM68KIO(address, 0xFFFF);
		static uint32_t dbgIOReadCount = 0;
		if(dbgIOReadCount < 50) {
			GENESIS_DBG("M68KRead I/O: addr=0x%08X upper=%d lower=%d result=0x%04X",
				address, upper, lower, result);
			dbgIOReadCount++;
		}
		return result;
	}

	//0xC00000-0xDFFFFF: VDP
	if(address >= 0xC00000 && address <= 0xDFFFFF) {
		if(!_vdpEnable[0] || !_vdpEnable[1]) return 0xFFFF;
		//VDP data port: 0xC00000-0xC00003
		if(address <= 0xC00003) {
			return _vdp->ReadDataPort();
		}
		//VDP control port: 0xC00004-0xC00007
		if(address <= 0xC00007) {
			return _vdp->ReadControlPort();
		}
		//HV counter: 0xC00008-0xC0000B
		if(address <= 0xC0000B) {
			return _vdp->Read(address);
		}
		//Other VDP mirrors
		return _vdp->Read(address);
	}

	//0xE00000-0xFFFFFF: Work RAM (64KB, mirrored)
	if(address >= 0xE00000) {
		uint32_t offset = address & 0xFFFF;
		uint16_t data = ((uint16_t)_m68kRam[offset]) << 8 | _m68kRam[offset + 1];
		return data;
	}

	return 0xFFFF; //open bus
}

// ============================================================================
// M68K bus write
// ============================================================================

void GenesisMemoryManager::M68KWrite(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data)
{
	address &= 0x00FFFFFE;

	//0x000000-0x3FFFFF: Cartridge area (SRAM writes)
	if(address < 0x400000) {
		if(address >= _sramStart && address < _sramStart + _sramSize && _sram && _sramEnable) {
			WriteSramWord(address, data, upper, lower);
		}
		//ROM writes ignored
		return;
	}

	//0xA00000-0xA0FFFF: Z80 bus window
	if(address >= 0xA00000 && address <= 0xA0FFFF) {
		if(!_busreqAck && !_resetLine) return;
		//Z80 RAM write
		if(address <= 0xA01FFF) {
			uint32_t offset = address & 0x1FFF;
			//M68K writes to Z80 RAM are byte-accessible
			if(lower) _z80Ram[offset] = data & 0xFF;
			if(upper) _z80Ram[offset] = (data >> 8) & 0xFF;
		}
		//0xA04000-0xA040FF: YM2612
		if(address >= 0xA04000 && address <= 0xA040FF) {
			if(address <= 0xA04003) {
				//Port 0 address/data
				if(address == 0xA04000 || address == 0xA04001) {
					//Address port 0
					_ym2612->WriteAddress(0, data & 0xFF);
				} else {
					//Data port 0
					_ym2612->WriteData(0, data & 0xFF);
				}
			} else if(address <= 0xA04007) {
				//Port 1 address/data
				if(address == 0xA04004 || address == 0xA04005) {
					_ym2612->WriteAddress(1, data & 0xFF);
				} else {
					_ym2612->WriteData(1, data & 0xFF);
				}
			}
		}
		return;
	}

	//0xA10000-0xA1FFFF: I/O region
	if(address >= 0xA10000 && address <= 0xA1FFFF) {
		WriteM68KIO(address, upper, lower, data);
		return;
	}

	//0xC00000-0xDFFFFF: VDP
	if(address >= 0xC00000 && address <= 0xDFFFFF) {
		if(!_vdpEnable[0] || !_vdpEnable[1]) return;
		_vdp->Write(address, data);
		return;
	}

	//0xE00000-0xFFFFFF: Work RAM
	if(address >= 0xE00000) {
		uint32_t offset = address & 0xFFFE;
		if(upper) _m68kRam[offset] = (data >> 8) & 0xFF;
		if(lower) _m68kRam[offset + 1] = data & 0xFF;
		return;
	}
}

// ============================================================================
// M68K I/O region read (0xA10000-0xA1FFFF)
// ============================================================================

uint16_t GenesisMemoryManager::ReadM68KIO(uint32_t address, uint16_t openBus)
{
	//0xA10000-0xA100FF: I/O ports (mirrored every 0x20)
	//Ares behavior: for word reads, lower byte is copied to upper byte
	//(data.byte(1) = data.byte(0)). This is critical for TMSS detection:
	//games do TST.L $A10008 to check if controller ports are initialized.
	//If upper byte is 0xFF (our old openBus behavior), the game thinks
	//TMSS BIOS already ran and skips the TMSS unlock, leading to a hang.
	if(address >= 0xA10000 && address <= 0xA100FF) {
		uint32_t reg = address & ~0x60; //mirror: a10020-a100ff -> a10000-a1001f
		uint8_t lo = 0;
		switch(reg) {
		case 0xA10000: {
			//Version register
			lo |= _io.version;        //bit 0: 0=Model1, 1=Model2+
			lo |= 0x20;               //bit 5: 1=no MegaCD
			lo |= (_console->GetRegion() == ConsoleRegion::Pal) ? 0x40 : 0x00;  //bit 6: 1=PAL
			lo |= (_console->GetRegion() != ConsoleRegion::NtscJapan) ? 0x80 : 0x00; //bit 7: 1=export
			break;
		}
		case 0xA10002: lo = _controlManager->ReadPort(0); break;
		case 0xA10004: lo = _controlManager->ReadPort(1); break;
		case 0xA10006: lo = _controlManager->ReadPort(2); break;
		case 0xA10008: lo = _controlManager->ReadControl(0); break;
		case 0xA1000A: lo = _controlManager->ReadControl(1); break;
		case 0xA1000C: lo = _controlManager->ReadControl(2); break;
		default: return openBus;
		}
		//Mirror lower byte to upper byte (ares: data.byte(1) = data.byte(0))
		return ((uint16_t)lo << 8) | lo;
	}

	//0xA11100-0xA111FF: Z80 bus request status
	if(address >= 0xA11100 && address <= 0xA111FF) {
		uint16_t data = openBus;
		data = (data & 0xFEFF) | (_busreqAck ? 0x0000 : 0x0100); //bit 8: 0=Z80 has bus, 1=Z80 granted
		return data;
	}

	return openBus;
}

// ============================================================================
// M68K I/O region write (0xA10000-0xA1FFFF)
// ============================================================================

void GenesisMemoryManager::WriteM68KIO(uint32_t address, uint8_t upper, uint8_t lower, uint16_t data)
{
	//0xA10000-0xA100FF: I/O ports
	if(address >= 0xA10000 && address <= 0xA100FF) {
		if(!lower) return; //only lower byte writes are processed
		uint32_t reg = address & ~0x60;
		switch(reg) {
		case 0xA10002:
			_controlManager->WritePort(0, data & 0xFF);
			break;
		case 0xA10004:
			_controlManager->WritePort(1, data & 0xFF);
			break;
		case 0xA10006:
			_controlManager->WritePort(2, data & 0xFF);
			break;
		case 0xA10008:
			_controlManager->WriteControl(0, data & 0xFF);
			break;
		case 0xA1000A:
			_controlManager->WriteControl(1, data & 0xFF);
			break;
		case 0xA1000C:
			_controlManager->WriteControl(2, data & 0xFF);
			break;
		}
		return;
	}

	//0xA11100-0xA111FF: Z80 bus request
	if(address >= 0xA11100 && address <= 0xA111FF) {
		if(!upper) return; //upper byte only
		SetBusreq((data >> 8) & 1);
		return;
	}

	//0xA11200-0xA112FF: Z80 reset
	if(address >= 0xA11200 && address <= 0xA112FF) {
		if(!upper) return;
		SetReset((data >> 8) & 1);
		return;
	}

	//0xA14000-0xA140FF: TMSS register
	if(address >= 0xA14000 && address <= 0xA140FF) {
		if(!_tmssEnable) {
			GENESIS_DBG("TMSS write (tmss disabled): addr=0x%08X data=0x%04X", address, data);
			return;
		}
		if(address == 0xA14000) {
			if(upper && lower) _vdpEnable[0] = (data == 0x5345); //"SE"
		}
		if(address == 0xA14002) {
			if(upper && lower) _vdpEnable[1] = (data == 0x4741); //"GA"
		}
		GENESIS_DBG("TMSS write: addr=0x%08X data=0x%04X vdpEnable=[%d,%d]",
			address, data, _vdpEnable[0], _vdpEnable[1]);
		return;
	}

	//0xA14100: TMSS ROM enable
	if(address >= 0xA14100 && address <= 0xA141FF) {
		if(!_tmssEnable) return;
		if(lower) _romEnable = data & 1;
		return;
	}

	//0xA130F0: Banked cartridge control
	if(address >= 0xA130F0 && address <= 0xA130FF) {
		if(!lower) return;
		//SRAM enable / write protect
		if(address == 0xA130F0) {
			_sramEnable = (data & 1) != 0;
			_sramWritable = (data & 2) == 0;
		}
		//Bank registers are handled in ReadRomWord/WriteSramWord via the cartridge
		//For now, standard cartridge doesn't use banks
		return;
	}
}

// ============================================================================
// ROM access helpers
// ============================================================================

uint16_t GenesisMemoryManager::ReadRomWord(uint32_t address)
{
	if(address + 1 >= _romSize) return 0xFFFF;
	return ((uint16_t)_rom[address] << 8) | _rom[address + 1];
}

uint16_t GenesisMemoryManager::ReadSramWord(uint32_t address)
{
	uint32_t offset = address - _sramStart;
	if(offset >= _sramSize) return 0xFFFF;
	//SRAM is byte-addressed; return as big-endian word
	uint16_t data = 0xFFFF;
	if(offset < _sramSize) {
		data = ((uint16_t)_sram[offset]) << 8;
		if(offset + 1 < _sramSize) {
			data |= _sram[offset + 1];
		}
	}
	return data;
}

void GenesisMemoryManager::WriteSramWord(uint32_t address, uint16_t data, uint8_t upper, uint8_t lower)
{
	if(!_sramWritable) return;
	uint32_t offset = address - _sramStart;
	if(upper && offset < _sramSize) _sram[offset] = (data >> 8) & 0xFF;
	if(lower && offset + 1 < _sramSize) _sram[offset + 1] = data & 0xFF;
}

// ============================================================================
// Z80 bus read
// ============================================================================

uint8_t GenesisMemoryManager::Z80Read(uint16_t address)
{
	//0x0000-0x1FFF: Z80 RAM (8KB, mirrored at 0x2000-0x3FFF)
	if(address <= 0x3FFF) {
		return _z80Ram[address & 0x1FFF];
	}

	//0x4000-0x5FFF: YM2612
	if(address >= 0x4000 && address <= 0x5FFF) {
		uint16_t reg = 0x4000 | (address & 3);
		switch(reg) {
		case 0x4000: //YM2612 port 0 address
		case 0x4002: //YM2612 port 1 address
			return 0xFF; //write-only
		case 0x4001: //YM2612 port 0 data (status)
		case 0x4003: //YM2612 port 1 data (status)
			return _ym2612->ReadStatus();
		}
		return 0xFF;
	}

	//0x6000-0x60FF: Bank register
	if(address >= 0x6000 && address <= 0x60FF) {
		return 0xFF; //write-only
	}

	//0x7F00-0x7FFF: VDP access
	if(address >= 0x7F00 && address <= 0x7FFF) {
		uint32_t m68kAddr = 0xC00000 | (address & 0xFF);
		return Z80ReadExternal(m68kAddr);
	}

	//0x8000-0xFFFF: M68K bus window (banked)
	if(address >= 0x8000) {
		uint32_t m68kAddr = (_z80Bank << 15) | (address & 0x7FFF);
		return Z80ReadExternal(m68kAddr);
	}

	return 0xFF;
}

// ============================================================================
// Z80 bus write
// ============================================================================

void GenesisMemoryManager::Z80Write(uint16_t address, uint8_t data)
{
	//0x0000-0x3FFF: Z80 RAM
	if(address <= 0x3FFF) {
		_z80Ram[address & 0x1FFF] = data;
		return;
	}

	//0x4000-0x5FFF: YM2612
	if(address >= 0x4000 && address <= 0x5FFF) {
		uint16_t reg = 0x4000 | (address & 3);
		switch(reg) {
		case 0x4000: _ym2612->WriteAddress(0, data); break;
		case 0x4001: _ym2612->WriteData(0, data); break;
		case 0x4002: _ym2612->WriteAddress(1, data); break;
		case 0x4003: _ym2612->WriteData(1, data); break;
		}
		return;
	}

	//0x6000-0x60FF: Bank register
	if(address >= 0x6000 && address <= 0x60FF) {
		_z80Bank = ((data & 1) << 8) | (_z80Bank >> 1);
		return;
	}

	//0x7F00-0x7FFF: VDP access
	if(address >= 0x7F00 && address <= 0x7FFF) {
		uint32_t m68kAddr = 0xC00000 | (address & 0xFF);
		Z80WriteExternal(m68kAddr, data);
		return;
	}

	//0x8000-0xFFFF: M68K bus window (banked)
	if(address >= 0x8000) {
		uint32_t m68kAddr = (_z80Bank << 15) | (address & 0x7FFF);
		Z80WriteExternal(m68kAddr, data);
		return;
	}
}

// ============================================================================
// Z80 external bus access (through M68K bus)
// ============================================================================

uint8_t GenesisMemoryManager::Z80ReadExternal(uint32_t m68kAddress)
{
	//Z80 cannot read M68K RAM — returns open bus
	if(m68kAddress >= 0xE00000 && m68kAddress <= 0xFFFFFF) {
		return 0xFF;
	}

	//Z80 can access: ROM (0x000000-0x9FFFFF), I/O (0xA10000-0xA1FFFF),
	//VDP (0xC00000-0xC000FF)
	bool accessible =
		(m68kAddress < 0xA00000) ||
		(m68kAddress >= 0xA10000 && m68kAddress <= 0xA1FFFF) ||
		(m68kAddress >= 0xC00000 && m68kAddress <= 0xC000FF);

	if(!accessible) return 0xFF;

	//Perform a word read on the M68K bus and return the appropriate byte
	uint16_t word = M68KRead(1, 1, m68kAddress & ~1);
	if(m68kAddress & 1) {
		return word & 0xFF; //odd address = low byte
	} else {
		return (word >> 8) & 0xFF; //even address = high byte
	}
}

void GenesisMemoryManager::Z80WriteExternal(uint32_t m68kAddress, uint8_t data)
{
	//Z80 can write to: ROM area (for SRAM), I/O, VDP, M68K RAM
	bool accessible =
		(m68kAddress < 0xA00000) ||
		(m68kAddress >= 0xA10000 && m68kAddress <= 0xA1FFFF) ||
		(m68kAddress >= 0xC00000 && m68kAddress <= 0xC000FF) ||
		(m68kAddress >= 0xE00000 && m68kAddress <= 0xFFFFFF);

	if(!accessible) return;

	//Write as a word with the byte in both positions (ares pattern)
	uint16_t word = (data << 8) | data;
	if(m68kAddress & 1) {
		M68KWrite(0, 1, m68kAddress & ~1, word);
	} else {
		M68KWrite(1, 0, m68kAddress & ~1, word);
	}
}

// ============================================================================
// Bus arbitration
// ============================================================================

void GenesisMemoryManager::SetBusreq(bool line)
{
	_busreqLine = line;
	if(!line) {
		//Bus request released — Z80 gets bus back after a short delay
		_busreqAck = false;
	}
	//Z80 will acknowledge bus request when it reaches a suitable point
	_z80->SetBusreq(line);
	//Immediately acknowledge for simplicity (ares waits for Z80 to reach
	//a bus cycle boundary, but in our explicit-clock model the Z80 doesn't
	//actually race the M68K — it's always in sync)
	if(line) {
		_busreqAck = true;
	}
}

void GenesisMemoryManager::SetReset(bool line)
{
	_resetLine = line;
	_z80->SetReset(line);
	if(line) {
		//Z80 is held in reset — bus is granted to M68K
		_busreqAck = true;
	}
}

bool GenesisMemoryManager::IsBusGranted() const
{
	return !_busreqAck;
}

// ============================================================================
// VDP DMA read
// ============================================================================

uint16_t GenesisMemoryManager::DmaRead(uint32_t address)
{
	//VDP DMA reads from the M68K bus (same as M68KRead but without
	//side effects like register access — just ROM/RAM reads)
	address &= 0x00FFFFFE;

	//ROM (0x000000-0x3FFFFF) and its mirrors (0x400000-0x7FFFFF, 0x800000-0xBFFFFF)
	//On real Genesis, ROM appears at every 4MB region except VDP/Z80/RAM areas
	if(address < 0xC00000 && address >= 0x400000) {
		//Mirror ROM into this region
		return ReadRomWord(address & 0x3FFFFE);
	}
	if(address < 0x400000) {
		return ReadRomWord(address);
	}

	//M68K RAM (64KB, mirrored across 0xE00000-0xFFFFFF)
	if(address >= 0xE00000) {
		uint32_t offset = address & 0xFFFF;
		return ((uint16_t)_m68kRam[offset] << 8) | _m68kRam[(offset + 1) & 0xFFFF];
	}

	//I/O region — some games do DMA from version register
	if(address >= 0xA10000 && address <= 0xA1FFFF) {
		return ReadM68KIO(address, 0xFFFF);
	}

	return 0xFFFF;
}

// ============================================================================
// M68K interrupt polling
// ============================================================================

void GenesisMemoryManager::PollM68KInterrupts()
{
	PollM68KInterruptsBool();
}

bool GenesisMemoryManager::PollM68KInterruptsBool()
{
	uint8_t ipl = _m68k->GetInterruptMask(); //current interrupt mask from SR

	//Vblank IRQ (level 6) — highest priority, check first
	if(_vdp->GetVblankIrq()) {
		if(6 > ipl) {
			GENESIS_DBG("PollM68KInterrupts: delivering VBlank IRQ! ipl=%u PC=0x%08X SR=0x%04X",
				ipl, _m68k->GetPC(), _m68k->GetSR());
			_vdp->AcknowledgeIrq(6);
			_m68k->Interrupt(GenesisM68K::VLevel6, 6);
			return true; //interrupt delivered
		} else {
			//VBlank is pending but blocked by SR.i — log once
			static bool dbgVblankBlocked = true;
			if(dbgVblankBlocked) {
				GENESIS_DBG("PollM68KInterrupts: VBlank IRQ BLOCKED by SR.i=%u (need i<6) PC=0x%08X SR=0x%04X",
					ipl, _m68k->GetPC(), _m68k->GetSR());
				dbgVblankBlocked = false;
			}
		}
	}
	//Hblank IRQ (level 4)
	if(_vdp->GetHblankIrq() && 4 > ipl) {
		_vdp->AcknowledgeIrq(4);
		_m68k->Interrupt(GenesisM68K::VLevel4, 4);
		return true;
	}
	//External IRQ (level 2) — lightgun etc, rarely used
	if(_vdp->GetExternalIrq() && 2 > ipl) {
		_vdp->AcknowledgeIrq(2);
		_m68k->Interrupt(GenesisM68K::VLevel2, 2);
		return true;
	}
	return false; //no interrupt delivered
}

// ============================================================================
// Battery (SRAM) save/load
// ============================================================================

void GenesisMemoryManager::LoadBattery()
{
	if(!_sram || _sramSize == 0) return;
	_emu->GetBatteryManager()->LoadBattery(".sav", _sram, _sramSize);
}

void GenesisMemoryManager::SaveBattery()
{
	if(!_sram || _sramSize == 0) return;
	if(_originalSram && memcmp(_sram, _originalSram, _sramSize) != 0) {
		_emu->GetBatteryManager()->SaveBattery(".sav", _sram, _sramSize);
	}
}

// ============================================================================
// Address translation
// ============================================================================

AddressInfo GenesisMemoryManager::GetAbsoluteAddress(uint32_t addr, CpuType cpuType)
{
	if(cpuType == CpuType::GenesisM68K) {
		addr &= 0x00FFFFFF;
		//ROM
		if(addr < _romSize && _rom) {
			return { (int32_t)addr, MemoryType::GenesisCartridgeRom };
		}
		//M68K RAM
		if(addr >= 0xE00000 && _m68kRam) {
			return { (int32_t)(addr & 0xFFFF), MemoryType::GenesisM68KRam };
		}
		//Z80 RAM (mapped at 0xA00000)
		if(addr >= 0xA00000 && addr <= 0xA01FFF && _z80Ram) {
			return { (int32_t)(addr & 0x1FFF), MemoryType::GenesisZ80Ram };
		}
		//SRAM
		if(addr >= _sramStart && addr < _sramStart + _sramSize && _sram) {
			return { (int32_t)(addr - _sramStart), MemoryType::GenesisCartridgeRam };
		}
		//VDP registers
		if(addr >= 0xC00000 && addr <= 0xC0001F) {
			return { (int32_t)(addr & 0x1F), MemoryType::GenesisVdpVram };
		}
	} else if(cpuType == CpuType::GenesisZ80) {
		//Z80 RAM
		if(addr <= 0x3FFF && _z80Ram) {
			return { (int32_t)(addr & 0x1FFF), MemoryType::GenesisZ80Ram };
		}
		//M68K bus window
		if(addr >= 0x8000) {
			uint32_t m68kAddr = (_z80Bank << 15) | (addr & 0x7FFF);
			return GetAbsoluteAddress(m68kAddr, CpuType::GenesisM68K);
		}
	}

	return { -1, MemoryType::None };
}

AddressInfo GenesisMemoryManager::GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType)
{
	//Reverse mapping of GetAbsoluteAddress: given an absolute address in a
	//memory region, find the CPU-relative address that maps to it.
	if(cpuType == CpuType::GenesisM68K) {
		switch(absAddress.Type) {
			case MemoryType::GenesisCartridgeRom:
				if(absAddress.Address < (int32_t)_romSize)
					return { absAddress.Address, MemoryType::GenesisCartridgeRom };
				break;
			case MemoryType::GenesisM68KRam:
				if(absAddress.Address < (int32_t)M68KRamSize)
					return { 0xE00000 + absAddress.Address, MemoryType::GenesisM68KRam };
				break;
			case MemoryType::GenesisCartridgeRam:
				if(_sram && absAddress.Address < (int32_t)_sramSize)
					return { (int32_t)(_sramStart + absAddress.Address), MemoryType::GenesisCartridgeRam };
				break;
			case MemoryType::GenesisZ80Ram:
				if(absAddress.Address < (int32_t)Z80RamSize)
					return { 0xA00000 + (absAddress.Address & 0x1FFF), MemoryType::GenesisZ80Ram };
				break;
			default: break;
		}
	} else if(cpuType == CpuType::GenesisZ80) {
		switch(absAddress.Type) {
			case MemoryType::GenesisZ80Ram:
				if(absAddress.Address < (int32_t)Z80RamSize)
					return { absAddress.Address & 0x1FFF, MemoryType::GenesisZ80Ram };
				break;
			default: break;
		}
	}
	return { -1, MemoryType::None };
}

// ============================================================================
// Serialization
// ============================================================================

void GenesisMemoryManager::Serialize(Serializer& s)
{
	SV(_z80Bank);
	SV(_busreqLine);
	SV(_busreqAck);
	SV(_resetLine);
	SV(_sramEnable);
	SV(_sramWritable);
	SV(_romEnable);
	SV(_vdpEnable[0]);
	SV(_vdpEnable[1]);
	SV(_io.version);

	SVArray(_m68kRam, M68KRamSize);
	SVArray(_z80Ram, Z80RamSize);
	if(_sram && _sramSize > 0) {
		SVArray(_sram, _sramSize);
	}
}
