#pragma once
#include "pch.h"
#include "Utilities/ISerializable.h"

class GenesisMcd;
struct DiscInfo;
class Serializer;

//NEC uPD75006 CD drive (CDD), 4-bit MCU HLE, ported from ares/md/mcd/cdd.cpp.
//
//The CDD is a 75 Hz device (one processing tick every ~166656 sub-CPU cycles).
//The sub-CPU communicates with it through 10 4-bit command nibbles
//(0xFF8042-0xFF804B, write) and 10 4-bit status nibbles (0xFF8038-0xFF8041,
//read). Writing the last command nibble (index 9) triggers process(), which
//decodes the command and prepares a status response. The response is delivered
//on the next 75 Hz tick via the level-4 interrupt (GenesisMcdIrq::cdd).
//
//Phase B scope: drive state machine (Stopped/ReadingTOC/Paused/Playing/Seeking),
//TOC queries (Request commands), seek/play/pause, sector advance, and CDC
//decode triggering for data tracks. CD-DA audio (sample/DAC) is stubbed — the
//DAC registers (0xFF8034) are stored but produce no audio until Phase D.
//Subcode reading is stubbed (no level-6 IRQ) until needed.
class GenesisMcdCdd final : public ISerializable
{
public:
	void SetMcd(GenesisMcd* mcd) { _mcd = mcd; }
	void SetDisc(DiscInfo* disc) { _disc = disc; }

	//ares MCD::CDD::power (calls Insert)
	void Power(bool reset);

	//ares MCD::CDD::insert — initialize from the loaded disc (build TOC state).
	void Insert();

	//ares MCD::CDD::eject
	void Eject();

	//ares MCD::CDD::clock — 75 Hz processing. Called every 384 sub-CPU cycles
	//(after the 434-tick divider) from GenesisMcd::Step().
	void Clock();

	//ares MCD::CDD::process — decode the command nibbles, prepare status.
	//Triggered when the sub-CPU writes the last command nibble (index 9).
	void Process();

	//ares MCD::CDD::advance — step the sector forward during Playing.
	void Advance();

	//--- register accessors for the IO handlers ---

	//0xFF8034: DAC rate/deemphasis/attenuator (Phase D stores but no audio).
	void WriteDac(uint16_t data);
	uint16_t ReadDac() const;

	//0xFF8036: CDD control / status.
	uint16_t ReadControl() const;
	void WriteControl(uint8_t lower, uint16_t data);

	//0xFF8038-0xFF8041: status nibbles (read-only, 10 nibbles packed 2/word).
	uint16_t ReadStatus(uint32_t index) const;

	//0xFF8042-0xFF804B: command nibbles (write, 10 nibbles packed 2/word).
	//lower/upper are byte-select flags; data is the 16-bit word. Writing the
	//last nibble (index|1 == 9) triggers Process().
	void WriteCommand(uint32_t index, bool lower, bool upper, uint16_t data);

	//Command nibble read-back (for 0xFF8042-0xFF804B reads). ares returns the
	//4-bit command value the sub-CPU previously wrote. index is 0-9.
	uint8_t ReadCommandNibble(uint32_t index) const { return _command[index < 10 ? index : 0] & 0x0F; }

	//0xFF8068: subcode position (Phase B: returns 0).
	uint16_t ReadSubcodePosition() const { return 0; }

	//0xFF8100-0xFF81FF: subcode data (Phase B: returns 0).
	uint16_t ReadSubcodeData(uint32_t index) const { (void)index; return 0; }

	//ISerializable
	void Serialize(Serializer& s) override;

	//Status enum (ares CDD::Status)
	enum Status : uint8_t {
		Stopped = 0x0, Playing = 0x1, Seeking = 0x2, Scanning = 0x3,
		Paused = 0x4, DoorOpened = 0x5, ChecksumError = 0x6, CommandError = 0x7,
		FunctionError = 0x8, ReadingTOC = 0x9, Tracking = 0xA, NoDisc = 0xB,
		LeadOut = 0xC, LeadIn = 0xD, TrayMoving = 0xE, Test = 0xF,
	};

private:
	GenesisMcd* _mcd = nullptr;
	DiscInfo* _disc = nullptr;

	//75 Hz divider (counts sub-CPU/384 ticks; fires at 434).
	uint16_t _counter = 0;

	bool _hostClockEnable = false;
	bool _statusPending = false;

	uint8_t _status[10] = {};   //4-bit status nibbles
	uint8_t _command[10] = {};  //4-bit command nibbles

	//DAC state (Phase D; stored but not audio-active in Phase B).
	struct DAC {
		bool rate = false;         //0 = normal, 1 = double
		uint8_t deemphasis = 0;    //0..3
		uint16_t attenuator = 0x4000;
		uint16_t attenuated = 0x4000;
	} _dac;

	//Drive IO state (ares CDD::IO).
	struct IO {
		uint8_t status = NoDisc;
		uint8_t seeking = 0;     //status after seeking (Playing or Paused)
		uint16_t latency = 0;
		int32_t sector = 0;      //current absolute LBA (DiscInfo convention)
		int32_t sectorRepeatCount = 0;
		uint16_t sample = 0;     //current audio sample within the sector
		uint8_t track = 0;       //current track# (1-based)
		bool tocRead = false;
		uint8_t subcodePosition = 0;
	} _io;

	//--- disc/track adapters (DiscInfo → ares CD::Session equivalent) ---
	bool   HasDisc() const;
	int32_t GetTrackFirstSector(uint32_t track) const;  //track is 1-based
	bool   IsTrackAudio(uint32_t track) const;
	bool   IsTrackData(uint32_t track) const;
	uint32_t GetFirstTrack() const { return 1; }
	uint32_t GetLastTrack() const;
	int32_t InTrack(int32_t sector) const;    //returns 1-based track# or -1
	bool   InLeadIn(int32_t sector) const;    //Phase B: always false
	bool   InLeadOut(int32_t sector) const;
	int32_t GetLeadOutLba() const;
	double Position(int32_t sector) const;     //normalized [0,1] for seek latency

	//Seek latency helper.
	uint16_t SeekLatency(int32_t fromSector, int32_t toSector) const;

	//Checksum (ares CDD::checksum / valid).
	void ComputeChecksum();
	bool ValidateCommand() const;

	//Drive control helpers.
	void Stop();
	void Play();
	void Pause();
};
