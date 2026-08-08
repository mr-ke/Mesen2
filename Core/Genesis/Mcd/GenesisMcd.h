#pragma once
#include "pch.h"
#include "Utilities/ISerializable.h"
#include "Shared/SettingTypes.h"
#include "Genesis/Mcd/GenesisMcdTypes.h"
#include "Genesis/Mcd/GenesisMcdIrq.h"
#include "Genesis/Mcd/GenesisMcdCdc.h"
#include "Genesis/Mcd/GenesisMcdCdd.h"
#include "Genesis/Mcd/GenesisMcdTimer.h"

class GenesisM68K;
class GenesisVdp;
class Emulator;
struct DiscInfo;

//Sega CD / Mega CD subsystem. Ports ares/md/mcd/mcd.cpp (+ bus-internal,
//bus-external, io-internal, io-external) into a native Mesen2 class.
//
//The MCD owns a SECOND M68000 (the "sub-CPU", 12.5 MHz) which reuses the
//existing GenesisM68K interpreter, plus its own memory map and gate-array
//registers. The main M68K reaches the MCD through the external bus/IO that
//GenesisMemoryManager routes to this class when a CD game is loaded.
//
//Phase A scope: sub-CPU + internal/external bus + communication registers +
//reset/run/halt + IRQ controller. The CDC/CDD/GPU/PCM/Timer subsystems are
//stubbed (register reads return open bus, writes are ignored) and arrive in
//later phases. MegaLD is not supported.
class GenesisMcd final : public ISerializable
{
public:
	GenesisMcd();
	~GenesisMcd();

	void SetEmulator(Emulator* emu) { _emu = emu; }
	void SetVdp(GenesisVdp* vdp) { _vdp = vdp; }
	void SetRegion(ConsoleRegion r) { _region = r; }

	//Load the region-appropriate 128KB BIOS into _bios. Returns false (with an
	//error message) if not found in the firmware folder. Mirrors PCE firmware
	//loading via FirmwareHelper / FolderUtilities::GetFirmwareFolder().
	bool LoadBios();

	//CUE disc (parsed by GenesisConsole; passed to the CDD for TOC/sector reads).
	void SetDisc(DiscInfo* disc) { _disc = disc; _cdd.SetDisc(disc); }

	//Power-on (reset=false) / soft reset (reset=true). ares MCD::power().
	void Power(bool reset);

	//ares MCD::resetCpu: re-power the sub-CPU (reloads its reset vector from
	//PRAM[0..6] via the internal bus) and raise the reset IRQ.
	void ResetCpu();

	//Advance sub-CPU peripherals by `clocks` sub-CPU cycles. ares MCD::step().
	//Phase A: accumulates dividers only (no peripherals clocked yet).
	void Step(uint32_t clocks);

	//Sub-CPU interrupt poll — ares MCD::main() priority dispatch. Bound as the
	//sub-CPU's CheckInterrupts callback. Returns true if a source was serviced.
	bool CheckSubCpuInterrupts();

	bool IsHalted() const { return _io.halt; }
	GenesisM68K* GetSubCpu() { return _subM68k.get(); }
	bool HasBios() const { return !_bios.empty(); }

	//Memory accessors (for emulator registration / debugger).
	//Word memories are big-endian uint16_t arrays; cast to uint8_t* for
	//registration with RegisterMemory.
	uint16_t* GetBiosData() { return _bios.data(); }
	uint16_t* GetPramData() { return _pram.data(); }
	uint16_t* GetWramData() { return _wram.data(); }
	uint8_t*  GetBramData() { return _bram.data(); }
	uint16_t* GetCdcRamData() { return _cdc.GetRamData(); }
	uint8_t*  GetPcmRamData() { return _pcmRam.data(); }

	static constexpr uint32_t BiosSize = 128 * 1024;
	static constexpr uint32_t PramSize = 512 * 1024;
	static constexpr uint32_t WramSize = 256 * 1024;
	static constexpr uint32_t BramSize = 8 * 1024;
	static constexpr uint32_t CdcRamSize = 16 * 1024;
	static constexpr uint32_t PcmRamSize = 64 * 1024;

	//--- callbacks from CDC/CDD/Timer subsystems (back-pointer helpers) ---
	//CDC poll(): raise/lower the level-5 cdc IRQ source.
	void RaiseCdcIrq()  { _irq.Raise(_irq.cdc); }
	void LowerCdcIrq()  { _irq.Lower(_irq.cdc); }
	//CDD: raise the level-4 cdd IRQ source.
	void RaiseCddIrq()  { _irq.Raise(_irq.cdd); }
	//Timer: raise the level-3 timer IRQ source.
	void RaiseTimerIrq(){ _irq.Raise(_irq.timer); }
	//Disc presence (for CDC decode + CDD).
	bool HasDisc() const { return _disc != nullptr; }
	//Read a raw 2352-byte sector from the disc into `out` (CDC decode).
	void ReadRawSector(uint32_t sector, uint8_t out[2352]);
	//CDD Playing: decode the data sector via the CDC.
	void DecodeCdcSector(int32_t sector) { _cdc.Decode(sector); }
	//CDC DMA destination 4 (PCM): Phase B writes directly to PCM RAM.
	void PcmDmaWrite(uint32_t address, uint16_t data);
	//CDC DMA destination 7 (WRAM): needs the current WRAM mode.
	bool GetWramMode() const { return _io.wramMode; }

	//--- Internal bus (sub-CPU) — bus-internal.cpp + io-internal.cpp
	uint16_t ReadInternal(uint8_t upper, uint8_t lower, uint32_t address);
	void WriteInternal(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data);

	//--- External bus (main CPU) — bus-external.cpp
	uint16_t ReadExternal(uint8_t upper, uint8_t lower, uint32_t address);
	void WriteExternal(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data);

	//--- External gate-array IO (main CPU 0xA12000-0xA1203F) — io-external.cpp
	uint16_t ReadExternalIO(uint8_t upper, uint8_t lower, uint32_t address);
	void WriteExternalIO(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data);

	//ISerializable
	void Serialize(Serializer& s) override;

private:
	Emulator* _emu = nullptr;
	GenesisVdp* _vdp = nullptr;
	ConsoleRegion _region = ConsoleRegion::Ntsc;
	DiscInfo* _disc = nullptr;

	unique_ptr<GenesisM68K> _subM68k;

	//Memories. Word memories (bios/pram/wram) are stored big-endian
	//(high byte first) so _mem[address>>1] matches the 68000 bus directly,
	//matching ares's n16 Memory arrays. BRAM/PCM-RAM are byte-wide.
	//CDC RAM (16KB) is owned by _cdc (matching ares cdc.ram).
	std::vector<uint16_t> _bios;     //64K words  (128KB)
	std::vector<uint16_t> _pram;     //256K words (512KB)
	std::vector<uint16_t> _wram;     //128K words (256KB)
	std::vector<uint8_t>  _bram;     //8KB
	std::vector<uint8_t>  _pcmRam;   //64KB

	McdIo _io;
	McdLed _led;
	McdCounter _counter;
	McdCommunication _communication;
	GenesisMcdIrq _irq;

	//CD subsystems (Phase B). Each holds a back-pointer to this for IRQ +
	//bus/disc access, set in WireSubCpuCallbacks().
	GenesisMcdCdc _cdc;
	GenesisMcdCdd _cdd;
	GenesisMcdTimer _timer;

	//io-internal.cpp
	uint16_t ReadIO(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data);
	void WriteIO(uint8_t upper, uint8_t lower, uint32_t address, uint16_t data);

	//1M-mode cell-mapped window address translation (bus-external.cpp).
	static uint32_t CellMapAddress(uint32_t address);

	void WireSubCpuCallbacks();
};
