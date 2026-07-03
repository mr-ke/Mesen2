#pragma once
#include "pch.h"
#include "Utilities/ISerializable.h"
#include "Shared/MemoryType.h"
#include "Shared/CheatManager.h"
#include "Shared/MemoryOperationType.h"
#include "Debugger/AddressInfo.h"

class Emulator;
class GenesisConsole;
class GenesisM68K;
class GenesisZ80;
class GenesisVdp;
class GenesisPsg;
class GenesisYm2612;
class GenesisControlManager;

// Genesis Memory Manager — M68K + Z80 bus arbiter.
//
// Handles the full 24-bit M68K address space and the 16-bit Z80 address space,
// routing reads/writes to the correct subsystem (ROM, RAM, VDP, YM2612, PSG,
// I/O ports, Z80 bus).  Also manages bus arbitration (Z80 bus request / grant),
// the Z80 bank register, and TMSS enable logic.
//
// Design follows the SmsMemoryManager pattern: raw pointers to subsystems
// (owned by GenesisConsole), no ares types.  The M68K bus uses word-granularity
// read/write callbacks set during Init(); the Z80 bus uses byte-granularity.

class GenesisMemoryManager final : public ISerializable
{
public:
	GenesisMemoryManager();
	~GenesisMemoryManager();

	void Init(Emulator* emu, GenesisConsole* console,
		GenesisM68K* m68k, GenesisZ80* z80, GenesisVdp* vdp,
		GenesisPsg* psg, GenesisYm2612* ym2612,
		GenesisControlManager* controlManager,
		uint8_t* rom, uint32_t romSize,
		uint8_t* sram, uint32_t sramSize,
		uint32_t sramStart, bool sramWritable);

	void Reset();

	//--- M68K bus interface (word-granularity, set on GenesisM68K) ---
	uint16_t M68KRead(uint8_t upper, uint8_t lower, uint32_t address);
	void M68KWrite(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data);

	//--- Z80 bus interface (byte-granularity, set on GenesisZ80) ---
	uint8_t Z80Read(uint16_t address);
	void Z80Write(uint16_t address, uint8_t data);

	//--- Z80 bus arbitration ---
	void SetBusreq(bool line);   //from M68K -> Z80 bus request
	void SetReset(bool line);    //from M68K -> Z80 reset
	bool IsBusGranted() const;   //true when Z80 owns the bus

	//--- VDP DMA read (M68K bus, used during 68K->VDP DMA) ---
	uint16_t DmaRead(uint32_t address);

	//--- M68K interrupt polling ---
	void PollM68KInterrupts();
	bool PollM68KInterruptsBool();  //returns true if an interrupt was delivered

	//--- SRAM / Battery ---
	void LoadBattery();
	void SaveBattery();

	//--- Address translation (debugger / cheats) ---
	AddressInfo GetAbsoluteAddress(uint32_t addr, CpuType cpuType);
	AddressInfo GetRelativeAddress(AddressInfo& absAddress, CpuType cpuType);

	//--- ISerializable ---
	void Serialize(Serializer& s) override;

private:
	Emulator* _emu = nullptr;
	GenesisConsole* _console = nullptr;
	GenesisM68K* _m68k = nullptr;
	GenesisZ80* _z80 = nullptr;
	GenesisVdp* _vdp = nullptr;
	GenesisPsg* _psg = nullptr;
	GenesisYm2612* _ym2612 = nullptr;
	GenesisControlManager* _controlManager = nullptr;

	//Cartridge ROM (word-accessible, byte-swapped for 68000 big-endian)
	uint8_t* _rom = nullptr;
	uint32_t _romSize = 0;

	//M68K work RAM (64KB)
	static constexpr uint32_t M68KRamSize = 0x10000;
	uint8_t* _m68kRam = nullptr;

	//Z80 RAM (8KB)
	static constexpr uint32_t Z80RamSize = 0x2000;
	uint8_t* _z80Ram = nullptr;

	//Cartridge SRAM (battery-backed)
	uint8_t* _sram = nullptr;
	uint32_t _sramSize = 0;
	uint32_t _sramStart = 0;   //M68K address where SRAM window starts
	bool _sramWritable = true;
	bool _sramEnable = false;  //for banked cartridges
	uint8_t* _originalSram = nullptr;

	//Z80 bank register (determines which 32KB window of the M68K bus
	//the Z80 sees at 0x8000-0xFFFF)
	uint16_t _z80Bank = 0;

	//Bus arbitration state
	bool _busreqLine = false;   //M68K has requested the Z80 bus
	bool _busreqAck = false;    //Z80 has acknowledged bus request
	bool _resetLine = false;    //Z80 is held in reset

	//TMSS state
	bool _tmssEnable = false;   //true if TMSS ROM is present
	bool _vdpEnable[2] = {true, true};  //TMSS VDP enable latch
	bool _romEnable = true;     //TMSS ROM enable

	//M68K I/O register state
	struct IO {
		uint8_t version = 0;     //0=Model 1, 1=Model 2+
	} _io;

	//--- Internal helpers ---
	uint16_t ReadRomWord(uint32_t address);
	uint16_t ReadSramWord(uint32_t address);
	void WriteSramWord(uint32_t address, uint16_t data, uint8_t upper, uint8_t lower);
	uint16_t ReadM68KIO(uint32_t address, uint16_t openBus);
	void WriteM68KIO(uint32_t address, uint8_t upper, uint8_t lower, uint16_t data);

	//Z80 external bus access (through M68K bus, with arbitration)
	uint8_t Z80ReadExternal(uint32_t m68kAddress);
	void Z80WriteExternal(uint32_t m68kAddress, uint8_t data);
};
