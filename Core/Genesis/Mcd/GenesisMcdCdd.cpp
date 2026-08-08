#include "pch.h"
#include "Genesis/Mcd/GenesisMcdCdd.h"
#include "Genesis/Mcd/GenesisMcd.h"
#include "Genesis/Mcd/GenesisMcdIrq.h"
#include "Shared/CdReader.h"
#include "Utilities/Serializer.h"

//bit helpers
static inline bool cbit(uint32_t v, int b) { return (v >> b) & 1; }
static inline uint32_t cbits(uint32_t v, int lo, int hi) { return (v >> lo) & ((1u << (hi - lo + 1)) - 1u); }

//Convert a relative LBA delta to BCD MSF nibbles (minute/second/frame tens+units).
//Used for RelativeTime (delta from track start). No lead-in offset — the delta
//is already a pure frame count.
static inline void LbaToMsf(int32_t lba, uint8_t& mT, uint8_t& mU, uint8_t& sT, uint8_t& sU, uint8_t& fT, uint8_t& fU)
{
	if(lba < 0) lba = 0;
	uint32_t m = (uint32_t)(lba / 75 / 60);
	uint32_t s = (uint32_t)(lba / 75 % 60);
	uint32_t f = (uint32_t)(lba % 75);
	mT = (uint8_t)(m / 10); mU = (uint8_t)(m % 10);
	sT = (uint8_t)(s / 10); sU = (uint8_t)(s % 10);
	fT = (uint8_t)(f / 10); fU = (uint8_t)(f % 10);
}

//Convert an LBA (0 = first user sector) to ABSOLUTE BCD MSF nibbles.
//In the CD-DA absolute MSF convention, 00:02:00 (frame 150) = LBA 0.
//The BIOS sends absolute MSF in seek commands and expects absolute MSF in
//TOC/AbsoluteTime/DiscCompletion responses. The 150-frame lead-in offset
//converts our DiscInfo LBA to the absolute frame number the BIOS uses.
static inline void LbaToAbsoluteMsf(int32_t lba, uint8_t& mT, uint8_t& mU, uint8_t& sT, uint8_t& sU, uint8_t& fT, uint8_t& fU)
{
	lba += 150;  //LBA → absolute disc frame (00:02:00 = LBA 0)
	if(lba < 0) lba = 0;
	uint32_t m = (uint32_t)(lba / 75 / 60);
	uint32_t s = (uint32_t)(lba / 75 % 60);
	uint32_t f = (uint32_t)(lba % 75);
	mT = (uint8_t)(m / 10); mU = (uint8_t)(m % 10);
	sT = (uint8_t)(s / 10); sU = (uint8_t)(s % 10);
	fT = (uint8_t)(f / 10); fU = (uint8_t)(f % 10);
}

//Convert an ABSOLUTE MSF (as sent by the BIOS in seek commands) to LBA.
//00:02:00 (frame 150) → LBA 0. The result may be negative (pre-roll: the
//BIOS typically seeks a few frames before the target so the CDD lands on
//the target after the seek latency expires).
static inline int32_t MsfToLba(uint32_t minute, uint32_t second, uint32_t frame)
{
	return (int32_t)(minute * 60 * 75 + second * 75 + frame) - 150;
}

// ============================================================================
// disc/track adapters
// ============================================================================
bool GenesisMcdCdd::HasDisc() const { return _disc != nullptr && !_disc->Tracks.empty(); }

uint32_t GenesisMcdCdd::GetLastTrack() const
{
	return HasDisc() ? (uint32_t)_disc->Tracks.size() : 0;
}

int32_t GenesisMcdCdd::GetTrackFirstSector(uint32_t track) const
{
	//track is 1-based; DiscInfo tracks are 0-based.
	if(!HasDisc() || track < 1 || track > _disc->Tracks.size()) return 0;
	return (int32_t)_disc->Tracks[track - 1].FirstSector;
}

bool GenesisMcdCdd::IsTrackAudio(uint32_t track) const
{
	if(!HasDisc() || track < 1 || track > _disc->Tracks.size()) return false;
	return _disc->Tracks[track - 1].Format == TrackFormat::Audio;
}

bool GenesisMcdCdd::IsTrackData(uint32_t track) const
{
	return HasDisc() && !IsTrackAudio(track);
}

int32_t GenesisMcdCdd::InTrack(int32_t sector) const
{
	if(!HasDisc()) return -1;
	if(sector < 0) return -1;  //negative LBA = pre-roll / lead-in area
	int32_t t = _disc->GetTrack((uint32_t)sector);
	return t >= 0 ? t + 1 : -1;  //convert 0-based → 1-based
}

bool GenesisMcdCdd::InLeadIn(int32_t sector) const
{
	//DiscInfo cue/bin images have no lead-in data. Negative LBA values arise
	//from seek pre-roll (the BIOS seeks a few frames before 00:02:00). Treat
	//them as lead-in so TrackInformation reports track 0 during pre-roll,
	//matching ares (where io.sector < 150 is in the lead-in).
	return sector < 0;
}

bool GenesisMcdCdd::InLeadOut(int32_t sector) const
{
	if(!HasDisc()) return false;
	return sector > (int32_t)_disc->Tracks.back().LastSector;
}

int32_t GenesisMcdCdd::GetLeadOutLba() const
{
	if(!HasDisc()) return 0;
	return (int32_t)_disc->Tracks.back().LastSector + 1;
}

double GenesisMcdCdd::Position(int32_t sector) const
{
	//Normalized [0,1] disc radius for seek latency (ares CDD::position).
	//ares adds session.leadIn.lba to convert LBA → absolute disc frame.
	//Our LBA 0 = absolute frame 150, so we add 150.
	static const double sectors = 7500.0 + 330000.0 + 6750.0;
	static const double radius = 0.058 - 0.024;
	static const double innerRadius = 0.024 * 0.024;
	static const double outerRadius = 0.058 * 0.058;
	sector += 150;  //LBA → absolute disc frame
	if(sector < 0) sector = 0;
	return sqrt((double)sector / sectors * (outerRadius - innerRadius) + innerRadius) / radius;
}

uint16_t GenesisMcdCdd::SeekLatency(int32_t fromSector, int32_t toSector) const
{
	return (uint16_t)(11.0 + 112.5 * std::abs(Position(fromSector) - Position(toSector)));
}

// ============================================================================
// ares MCD::CDD::power
// ============================================================================
void GenesisMcdCdd::Power(bool reset)
{
	(void)reset;
	_counter = 0;
	_hostClockEnable = false;
	_statusPending = false;
	_dac = {};
	_io = {};
	for(auto& d : _status) d = 0;
	for(auto& d : _command) d = 0;
	Insert();
	ComputeChecksum();
}

// ============================================================================
// ares MCD::CDD::insert
// ============================================================================
void GenesisMcdCdd::Insert()
{
	if(!HasDisc()) {
		_io.status = NoDisc;
		return;
	}
	_io.status = ReadingTOC;
	_io.sector = GetTrackFirstSector(1);
	_io.sample = 0;
	_io.track = 1;
	_io.tocRead = false;
}

void GenesisMcdCdd::Eject()
{
	_io = {};
	_io.status = NoDisc;
}

// ============================================================================
// ares MCD::CDD::clock — 75 Hz processing
// ============================================================================
void GenesisMcdCdd::Clock()
{
	if(++_counter < 434) return;
	_counter = 0;

	//75 Hz reached. The CDD only processes when the host clock is enabled
	//(sub-CPU writes 0xFF8036 bit 2).
	if(!_hostClockEnable) return;

	if(_statusPending) {
		_statusPending = false;
		if(_mcd) _mcd->RaiseCddIrq();
	}

	switch(_io.status) {

	case Stopped: {
		_io.status = HasDisc() ? ReadingTOC : NoDisc;
		_io.sector = GetTrackFirstSector(1);
		_io.sample = 0;
		_io.track = 1;
	} break;

	case ReadingTOC: {
		_io.sector++;
		if(!InLeadIn(_io.sector)) {
			_io.status = Paused;
			int32_t t = InTrack(_io.sector);
			if(t > 0) _io.track = (uint8_t)t;
			_io.tocRead = true;
		}
	} break;

	case Playing: {
		//readSubcode() — Phase B: stubbed (no subcode IRQ).
		if(IsTrackAudio(_io.track)) break;
		//Data track: decode the sector via the CDC.
		if(_mcd) _mcd->DecodeCdcSector(_io.sector);
		Advance();
	} break;

	case Tracking:
	case Seeking: {
		if(_io.latency && --_io.latency) break;
		_io.status = _io.seeking;
		int32_t t = InTrack(_io.sector);
		if(t > 0) _io.track = (uint8_t)t;
	} break;

	default: break;
	}
}

// ============================================================================
// ares MCD::CDD::advance — step the sector forward during Playing
// ============================================================================
void GenesisMcdCdd::Advance()
{
	int32_t next = _io.sector + 1;

	//Pre-roll: sector is negative (seek landed a few frames before LBA 0).
	//Keep advancing toward LBA 0 without changing status.
	if(next < 0) {
		_io.sector = next;
		_io.sample = 0;
		return;
	}

	if(InTrack(next) > 0) {
		_io.sector = next;
		_io.sample = 0;
		return;
	}

	//Past the end of the disc
	_io.status = LeadOut;
	_io.track = 0xAA;
}

// ============================================================================
// ares MCD::CDD::process — command dispatch
// ============================================================================
void GenesisMcdCdd::Process()
{
	if(!ValidateCommand()) {
		_io.status = ChecksumError;
	} else {
		switch(_command[0]) {

		case 0x0: { //Idle
			//ares: fixes Lunar — if not seeking and status[1]==0xf, report track#.
			if(!_io.latency && _status[1] == 0xf) {
				_status[1] = 0x2;
				_status[2] = (uint8_t)(_io.track / 10);
				_status[3] = (uint8_t)(_io.track % 10);
			}
		} break;

		case 0x1: { //Stop
			Stop();
			_status[1] = 0x0;
			for(int i = 2; i <= 8; i++) _status[i] = 0;
		} break;

		case 0x2: { //Request
			switch(_command[3]) {

			case 0x0: { //AbsoluteTime — must return ABSOLUTE MSF (00:02:00 = LBA 0)
				//ares uses CD::MSF(io.sector) where io.sector is in absolute disc
				//frame coords (0=lead-in start, 150=first user sector). Our _io.sector
				//is DiscInfo LBA (0=first user sector), so we add 150 via
				//LbaToAbsoluteMsf to produce the same absolute MSF the BIOS expects.
				uint8_t mT, mU, sT, sU, fT, fU;
				LbaToAbsoluteMsf(_io.sector, mT, mU, sT, sU, fT, fU);
				_status[1] = _command[3];
				_status[2] = mT; _status[3] = mU;
				_status[4] = sT; _status[5] = sU;
				_status[6] = fT; _status[7] = fU;
				_status[8] = (uint8_t)(IsTrackData(_io.track) ? 0x4 : 0x0);
			} break;

			case 0x1: { //RelativeTime
				int32_t rel = _io.sector - GetTrackFirstSector(_io.track);
				uint8_t mT, mU, sT, sU, fT, fU;
				LbaToMsf(rel, mT, mU, sT, sU, fT, fU);
				_status[1] = _command[3];
				_status[2] = mT; _status[3] = mU;
				_status[4] = sT; _status[5] = sU;
				_status[6] = fT; _status[7] = fU;
				_status[8] = (uint8_t)(IsTrackData(_io.track) ? 0x4 : 0x0);
			} break;

			case 0x2: { //TrackInformation
				_status[1] = _command[3];
				_status[2] = (uint8_t)(_io.track / 10);
				_status[3] = (uint8_t)(_io.track % 10);
				_status[4] = 0; _status[5] = 0;
				_status[6] = 0; _status[7] = 0;
				_status[8] = 0;
				if(InLeadIn(_io.sector))  { _status[2] = 0; _status[3] = 0; }
				if(InLeadOut(_io.sector)) { _status[2] = 0xA; _status[3] = 0xA; }
			} break;

			case 0x3: { //DiscCompletionTime (lead-out time) — ABSOLUTE MSF
				uint8_t mT, mU, sT, sU, fT, fU;
				LbaToAbsoluteMsf(GetLeadOutLba(), mT, mU, sT, sU, fT, fU);
				_status[1] = _command[3];
				_status[2] = mT; _status[3] = mU;
				_status[4] = sT; _status[5] = sU;
				_status[6] = fT; _status[7] = fU;
				_status[8] = 0;
			} break;

			case 0x4: { //DiscTracks (first/last track numbers)
				uint32_t first = GetFirstTrack();
				uint32_t last = GetLastTrack();
				_status[1] = _command[3];
				_status[2] = (uint8_t)(first / 10); _status[3] = (uint8_t)(first % 10);
				_status[4] = (uint8_t)(last / 10);  _status[5] = (uint8_t)(last % 10);
				_status[6] = 0; _status[7] = 0;
				_status[8] = 0;
			} break;

			case 0x5: { //TrackStartTime — must return ABSOLUTE MSF (00:02:00 = LBA 0)
				//ares uses CD::MSF(session.tracks[track].indices[1].lba) where lba
				//is in absolute disc frame coords. Our GetTrackFirstSector returns
				//DiscInfo LBA (0=first user sector), so we add 150 via
				//LbaToAbsoluteMsf to match.
				if(_command[4] > 9 || _command[5] > 9) break;
				uint32_t track = _command[4] * 10 + _command[5];
				int32_t lba = GetTrackFirstSector(track);
				uint8_t mT, mU, sT, sU, fT, fU;
				LbaToAbsoluteMsf(lba, mT, mU, sT, sU, fT, fU);
				_status[1] = _command[3];
				_status[2] = mT; _status[3] = mU;
				_status[4] = sT; _status[5] = sU;
				_status[6] = fT; _status[7] = fU;
				if(IsTrackData(track)) _status[6] |= 0x8;  //bit 3 = data
				_status[8] = (uint8_t)(track % 10);
			} break;

			case 0x6: { //ErrorInformation
				_status[1] = _command[3];
				for(int i = 2; i <= 8; i++) _status[i] = 0;
			} break;

			default: break;
			}
		} break;

		case 0x3: { //SeekPlay
			uint32_t minute = _command[2] * 10 + _command[3];
			uint32_t second = _command[4] * 10 + _command[5];
			uint32_t frame  = _command[6] * 10 + _command[7];
			//ares cdd.cpp:437 subtracts 3 extra frames of CDD latency pre-roll
			//on top of the MSF→LBA conversion. The sub-CPU OS enables the CDC
			//decoder one 75Hz tick after "Playing" is reported, so the first
			//decode window is always missed; without this slack the disc-check
			//approach validator (success window = expected-4..expected-1)
			//consumes the target frame during setup and validation then starts
			//one frame AHEAD of exp2 → permanent FAIL-RE-SEEK loop.
			int32_t lba = MsfToLba(minute, second, frame) - 3;

			_counter = 0;
			_io.status = Seeking;
			_io.seeking = Playing;
			_io.latency = SeekLatency(_io.sector, lba);
			_io.sector = lba;
			_io.sample = 0;

			_status[1] = 0xf;
			for(int i = 2; i <= 8; i++) _status[i] = 0;
		} break;

		case 0x4: { //SeekPause
			uint32_t minute = _command[2] * 10 + _command[3];
			uint32_t second = _command[4] * 10 + _command[5];
			uint32_t frame  = _command[6] * 10 + _command[7];
			//ares cdd.cpp:457 — same 3-frame CDD latency pre-roll as SeekPlay.
			int32_t lba = MsfToLba(minute, second, frame) - 3;

			_counter = 0;
			_io.status = Seeking;
			_io.seeking = Paused;
			_io.latency = SeekLatency(_io.sector, lba);
			_io.sector = lba;
			_io.sample = 0;

			_status[1] = 0xf;
			for(int i = 2; i <= 8; i++) _status[i] = 0;
		} break;

		case 0x6: { //Pause
			Pause();
		} break;

		case 0x7: { //Play
			Play();
		} break;

		case 0xA: { //TrackSkip (treated as null seek — ares TODO)
			_counter = 0;
			_io.status = Tracking;
			_io.seeking = Paused;
			_io.latency = 0;
			_io.sample = 0;

			_status[1] = 0xf;
			for(int i = 2; i <= 8; i++) _status[i] = 0;
		} break;

		default:
			_io.status = CommandError;
			break;
		}
	}

	_status[0] = _io.status;
	ComputeChecksum();
	_statusPending = true;
}

// ============================================================================
// checksum / validation
// ============================================================================
bool GenesisMcdCdd::ValidateCommand() const
{
	uint8_t checksum = 0;
	for(uint32_t i = 0; i < 9; i++) checksum += _command[i];
	checksum = (~checksum) & 0x0F;
	return checksum == _command[9];
}

void GenesisMcdCdd::ComputeChecksum()
{
	uint8_t checksum = 0;
	for(uint32_t i = 0; i < 9; i++) checksum += _status[i];
	_status[9] = (~checksum) & 0x0F;
}

// ============================================================================
// drive control helpers
// ============================================================================
void GenesisMcdCdd::Stop()
{
	_io.status = HasDisc() ? Stopped : NoDisc;
}

void GenesisMcdCdd::Play()
{
	if(_io.status == Seeking) {
		_io.seeking = Playing;
	} else {
		_io.status = Playing;
	}
}

void GenesisMcdCdd::Pause()
{
	if(_io.status == Seeking) {
		_io.seeking = Paused;
	} else {
		_io.status = Paused;
	}
}

// ============================================================================
// register accessors
// ============================================================================

//0xFF8034: DAC configuration (Phase D: stored, no audio output).
void GenesisMcdCdd::WriteDac(uint16_t data)
{
	_dac.rate = cbit(data, 1);
	_dac.deemphasis = (uint8_t)cbits(data, 2, 3);
	_dac.attenuator = (_dac.attenuator & 0x003F) | ((data & 0xFFC0));
	//reconfigure() — Phase D: no audio filter to reset.
}

uint16_t GenesisMcdCdd::ReadDac() const
{
	//0xFF8034 read: bit 15 = end of fade data transfer (always 0).
	uint16_t data = 0;
	data |= (uint16_t)_dac.rate << 1;
	data |= (uint16_t)(_dac.deemphasis & 0x3) << 2;
	data |= (_dac.attenuator & 0xFFC0);
	return data;
}

//0xFF8036: CDD control / status.
uint16_t GenesisMcdCdd::ReadControl() const
{
	uint16_t data = 0;
	data |= (uint16_t)_hostClockEnable << 2;
	data |= (uint16_t)(IsTrackData(_io.track) ? 1 : 0) << 8;
	return data;
}

void GenesisMcdCdd::WriteControl(uint8_t lower, uint16_t data)
{
	if(lower) {
		if(!_hostClockEnable && cbit(data, 2)) {
			//ares: enabling the host clock raises the CDD IRQ immediately.
			_hostClockEnable = true;
			if(_mcd) _mcd->RaiseCddIrq();
		} else {
			_hostClockEnable = cbit(data, 2);
		}
		_counter = 0;
	}
}

//0xFF8038-0xFF8041: status nibbles (read-only).
//Each word holds two nibbles: lo = status[index|1], hi = status[index|0].
uint16_t GenesisMcdCdd::ReadStatus(uint32_t index) const
{
	uint16_t data = 0;
	data |= (uint16_t)(_status[index | 1] & 0x0F);
	data |= (uint16_t)(_status[index | 0] & 0x0F) << 8;
	return data;
}

//0xFF8042-0xFF804B: command nibbles (write). Writing index|1==9 triggers Process().
void GenesisMcdCdd::WriteCommand(uint32_t index, bool lower, bool upper, uint16_t data)
{
	if(lower) _command[index | 1] = (uint8_t)(data & 0x0F);
	if(upper) _command[index | 0] = (uint8_t)((data >> 8) & 0x0F);
	if(lower && (index | 1) == 9) Process();
}

// ============================================================================
// ISerializable
// ============================================================================
void GenesisMcdCdd::Serialize(Serializer& s)
{
	SV(_counter);
	SV(_hostClockEnable);
	SV(_statusPending);
	for(int i = 0; i < 10; i++) SVI(_status[i]);
	for(int i = 0; i < 10; i++) SVI(_command[i]);
	SV(_dac.rate); SV(_dac.deemphasis); SV(_dac.attenuator); SV(_dac.attenuated);
	SV(_io.status); SV(_io.seeking); SV(_io.latency); SV(_io.sector);
	SV(_io.sectorRepeatCount); SV(_io.sample); SV(_io.track); SV(_io.tocRead); SV(_io.subcodePosition);
}
