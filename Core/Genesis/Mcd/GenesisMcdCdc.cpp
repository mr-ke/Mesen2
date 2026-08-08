#include "pch.h"
#include "Genesis/Mcd/GenesisMcdCdc.h"
#include "Genesis/Mcd/GenesisMcd.h"
#include "Genesis/Mcd/GenesisMcdIrq.h"
#include "Utilities/Serializer.h"

//bit helpers mirroring ares n16 .bit(n)/.bit(lo,hi)
static inline bool cbit(uint32_t v, int b) { return (v >> b) & 1; }

// ============================================================================
// ares MCD::CDC::poll — recompute the level-5 IRQ line from sub-sources.
// ============================================================================
void GenesisMcdCdc::Poll()
{
	bool pending = false;
	pending |= _irqDecoder.enable  && _irqDecoder.pending;
	pending |= _irqTransfer.enable && _irqTransfer.pending;
	pending |= _irqCommand.enable  && _irqCommand.pending;
	if(_mcd) {
		if(pending) _mcd->RaiseCdcIrq();
		else        _mcd->LowerCdcIrq();
	}
}

// ============================================================================
// ares MCD::CDC::clock
// ============================================================================
void GenesisMcdCdc::Clock()
{
	_stopwatch = (_stopwatch + 1) & 0xFFF;
}

// ============================================================================
// ares MCD::CDC::power
// ============================================================================
void GenesisMcdCdc::Power(bool reset)
{
	(void)reset;
	_ram.assign(CdcRamWords, 0);
	_address = 0;
	_stopwatch = 0;
	_irqDecoder = {};
	_irqTransfer = {};
	_irqCommand = {};
	_command = {};
	_status = {};
	_transfer = {};
	_decoder = {};
	_header = {};
	_subheader = {};
	_control = {};
}

// ============================================================================
// ares MCD::CDC::decode(sector)
//
// Reads the raw 2352-byte sector from the disc and writes it into CDC RAM at
// the transfer pointer, with the 12-byte sync header wrapped to the tail
// (LC8951x quirk). Sets decoder.valid + the decoder IRQ sub-source.
// ============================================================================
void GenesisMcdCdc::Decode(int32_t sector)
{
	if(!_decoder.enable) return;
	if(!_mcd || !_mcd->HasDisc()) return;

	//Negative sectors arise from seek pre-roll (BIOS seeks a few frames before
	//00:02:00). The decoder IRQ still fires so the BIOS knows the CDD is alive,
	//but there's no disc data to read for lead-in frames.
	bool preRoll = (sector < 0);

	//The sector header (HEAD0-HEAD3) must contain ABSOLUTE MSF — the same
	//address a real CD-ROM encodes in the 4-byte header after the sync pattern.
	//In the CD-DA absolute convention, 00:02:00 (frame 150) = LBA 0 (first user
	//sector). Our DiscInfo LBA is 0-based on the first user sector, so we add
	//150 to convert to the absolute disc frame the BIOS expects. ares does the
	//same via CD::MSF(io.sector) where io.sector is already in absolute-frame
	//coordinates (0 = start of lead-in, 150 = first user sector).
	int32_t absFrame = sector + 150;
	if(absFrame < 0) absFrame = 0;  //lead-in clamp (avoids unsigned underflow)

	//MSF header in BCD (mode 1).
	uint32_t m = (uint32_t)(absFrame / 75 / 60);
	uint32_t s = (uint32_t)(absFrame / 75 % 60);
	uint32_t f = (uint32_t)(absFrame % 75);
	_header.minute = (uint8_t)(((m / 10) << 4) | (m % 10));
	_header.second = (uint8_t)(((s / 10) << 4) | (s % 10));
	_header.frame  = (uint8_t)(((f / 10) << 4) | (f % 10));
	_header.mode   = 0x01;

	_decoder.valid = true;
	_irqDecoder.pending = true;
	Poll();

	if(_control.writeRequest && !preRoll) {
		_transfer.pointer = (_transfer.pointer + 2352) & 0xFFFF;
		_transfer.target  = (_transfer.target  + 2352) & 0xFFFF;

		//Read the raw 2352-byte sector from the disc.
		uint8_t raw[2352];
		_mcd->ReadRawSector((uint32_t)sector, raw);

		//Sync header (first 12 bytes of the sector) → tail of the CDC RAM block.
		for(uint32_t i = 0; i < 12; i += 2) {
			uint16_t w = (uint16_t)((raw[i] << 8) | raw[i + 1]);
			uint32_t a = (_transfer.pointer + i + 2340) >> 1;
			_ram[a & (CdcRamWords - 1)] = w;
		}
		//Remaining 2340 bytes (header + user data + EDC/ECC) → start of the block.
		for(uint32_t i = 0; i < 2340; i += 2) {
			uint16_t w = (uint16_t)((raw[i + 12] << 8) | raw[i + 13]);
			uint32_t a = (_transfer.pointer + i) >> 1;
			_ram[a & (CdcRamWords - 1)] = w;
		}
	}
}

// ============================================================================
// ares MCD::CDC::read — register read at the current address.
// ============================================================================
uint8_t GenesisMcdCdc::Read()
{
	uint8_t data = 0xFF;

	switch(_address) {

	//COMIN: command input
	case 0x0: {
		if(_command.empty) { data = 0xFF; break; }
		data = _command.fifo[_command.read];
		_command.read = (_command.read + 1) & 7;
		if(_command.read == _command.write) {
			_command.empty = true;
			_irqCommand.pending = false;
			Poll();
		}
	} break;

	//IFSTAT: interface status
	case 0x1: {
		data  = 0;
		data |= (uint8_t)(!_status.active)       << 0;
		data |= (uint8_t)(!_transfer.active)     << 1;
		data |= (uint8_t)(!_status.busy)         << 2;
		data |= (uint8_t)(!_transfer.busy)       << 3;
		data |= 1                                  << 4;
		data |= (uint8_t)(!_irqDecoder.pending)  << 5;
		data |= (uint8_t)(!_irqTransfer.pending) << 6;
		data |= (uint8_t)(!_irqCommand.pending)  << 7;
	} break;

	//DBCL: data byte counter low
	case 0x2: data = (uint8_t)(_transfer.length & 0xFF); break;

	//DBCH: data byte counter high
	case 0x3: data = (uint8_t)((_transfer.length >> 8) & 0x0F); break;

	//HEAD0..HEAD3: header or subheader data
	case 0x4: data = !_control.head ? _header.minute   : _subheader.file;    break;
	case 0x5: data = !_control.head ? _header.second   : _subheader.channel; break;
	case 0x6: data = !_control.head ? _header.frame    : _subheader.submode; break;
	case 0x7: data = !_control.head ? _header.mode     : _subheader.coding;  break;

	//PTL/PTH: block pointer
	case 0x8: data = (uint8_t)(_transfer.pointer & 0xFF); break;
	case 0x9: data = (uint8_t)((_transfer.pointer >> 8) & 0xFF); break;

	//WAL/WAH: write address
	case 0xA: data = (uint8_t)(_transfer.target & 0xFF); break;
	case 0xB: data = (uint8_t)((_transfer.target >> 8) & 0xFF); break;

	//STAT0
	case 0xC:
		data = 0;
		data |= (uint8_t)_decoder.enable << 7;  //CRCOK
		break;

	//STAT1 (all error bits 0 — perfect-disc HLE)
	case 0xD: data = 0; break;

	//STAT2
	case 0xE:
		data = 0;
		data |= (uint8_t)_decoder.form << 2;
		data |= (uint8_t)_decoder.mode << 3;
		break;

	//STAT3
	case 0xF:
		data = 0;
		data |= (uint8_t)(!_decoder.valid) << 7;  //!VALST
		_decoder.valid = false;
		_irqDecoder.pending = false;
		Poll();
		break;
	}

	//COMIN reads do not increment; STAT3 (0xF) wraps to 0 (5-bit +1 = 0x10 → 0).
	if(_address) _address = (_address + 1) & 0x1F;
	return data;
}

// ============================================================================
// ares MCD::CDC::write — register write at the current address.
// ============================================================================
void GenesisMcdCdc::Write(uint8_t data)
{
	switch(_address) {

	//SBOUT: status byte output
	case 0x0: {
		if(_status.wait && _transfer.busy) break;
		if(_status.read == _status.write && !_status.empty) _status.read = (_status.read + 1) & 7;
		_status.fifo[_status.write] = data;
		_status.write = (_status.write + 1) & 7;
		_status.empty = false;
		_status.active = true;
		_status.busy = true;
	} break;

	//IFCTRL
	case 0x1: {
		_status.enable          = cbit(data, 0);     //SOUTEN
		_transfer.enable        = cbit(data, 1);     //DOUTEN
		_status.wait            = !cbit(data, 2);    //STWAI
		_transfer.wait          = !cbit(data, 3);    //DTWAI
		_control.commandBreak   = !cbit(data, 4);    //CMDBK
		_irqDecoder.enable      = cbit(data, 5);     //DECEIN
		_irqTransfer.enable     = cbit(data, 6);     //DTEIEN
		_irqCommand.enable      = cbit(data, 7);     //CMDIEN
		Poll();
		if(!_transfer.enable) TransferStop();
	} break;

	//DBCL/DBCH: data byte counter
	case 0x2: _transfer.length = (_transfer.length & 0xFF00) | data; break;
	case 0x3: _transfer.length = (_transfer.length & 0x00FF) | ((uint16_t)(data & 0x0F) << 8); break;

	//DACL/DACH: data address counter (CDC RAM source)
	case 0x4: _transfer.source = (_transfer.source & 0xFF00) | data; break;
	case 0x5: _transfer.source = (_transfer.source & 0x00FF) | ((uint16_t)data << 8); break;

	//DTRG: data trigger
	case 0x6: TransferStart(); break;

	//DTACK
	case 0x7: _irqTransfer.pending = false; Poll(); break;

	//WAL/WAH: write address
	case 0x8: _transfer.target = (_transfer.target & 0xFF00) | data; break;
	case 0x9: _transfer.target = (_transfer.target & 0x00FF) | ((uint16_t)data << 8); break;

	//CTRL0
	case 0xA: {
		_control.pCodeCorrection = cbit(data, 0);
		_control.qCodeCorrection = cbit(data, 1);
		_control.writeRequest    = cbit(data, 2);
		_control.erasureRequest  = cbit(data, 3);
		_control.autoCorrection  = cbit(data, 4);
		_control.errorCorrection = cbit(data, 5);
		_control.edcCorrection   = cbit(data, 6);
		_decoder.enable          = cbit(data, 7);  //DECEN
		_decoder.mode = _control.mode;
		_decoder.form = _control.form && _control.autoCorrection;
	} break;

	//CTRL1
	case 0xB: {
		_control.head            = cbit(data, 0);
		_control.modeByteCheck   = cbit(data, 1);
		_control.form            = cbit(data, 2);
		_control.mode            = cbit(data, 3);
		_control.correctionWrite = cbit(data, 4);
		_control.descramble      = cbit(data, 5);
		_control.syncDetection   = cbit(data, 6);
		_control.syncInterrupt   = cbit(data, 7);
		_decoder.mode = _control.mode;
		_decoder.form = _control.form && _control.autoCorrection;
	} break;

	//PTL/PTH: block pointer
	case 0xC: _transfer.pointer = (_transfer.pointer & 0xFF00) | data; break;
	case 0xD: _transfer.pointer = (_transfer.pointer & 0x00FF) | ((uint16_t)data << 8); break;

	//CTRL2
	case 0xE:
		_control.statusTrigger     = cbit(data, 0);
		_control.statusControl     = cbit(data, 1);
		_control.erasureCorrection = cbit(data, 2);
		break;

	//RESET: software reset
	case 0xF: {
		_status.active = false; _transfer.active = false;
		_status.busy = false;   _transfer.busy = false;
		_irqDecoder.pending = false; _irqTransfer.pending = false; _irqCommand.pending = false;
		_status.enable = false;  _transfer.enable = false;
		_status.wait = true;     _transfer.wait = true;
		_control.commandBreak = true;
		_irqDecoder.enable = false; _irqTransfer.enable = false; _irqCommand.enable = false;
		_control = {};
		_decoder = {};
		_header = {};
		_subheader = {};
		TransferStop();
		Poll();
	} break;
	}

	//SBOUT writes do not increment; RESET (0xF) wraps to 0.
	if(_address) _address = (_address + 1) & 0x1F;
}

// ============================================================================
// cdc-transfer.cpp
// ============================================================================

//ares MCD::CDC::Transfer::dma — one step (called every 6 sub-CPU cycles).
void GenesisMcdCdc::TransferDma()
{
	if(!_transfer.active) return;
	if(_transfer.destination != 4 && _transfer.destination != 5 && _transfer.destination != 7) return;

	uint16_t data = _ram[(_transfer.source >> 1) & (CdcRamWords - 1)];

	switch(_transfer.destination) {
	case 4: //PCM (Phase D). Phase B: write to PCM RAM buffer directly.
		if(_mcd) _mcd->PcmDmaWrite(_transfer.address, data);
		_transfer.address += 2;  //PCM DMA requires two 8-bit writes per transfer
		break;
	case 5: //PRAM
		if(_mcd) _mcd->WriteInternal(1, 1, 0x000000 | (_transfer.address & ~1u), data);
		break;
	case 7: //WRAM
		if(_mcd) {
			if(!_mcd->GetWramMode()) {
				_mcd->WriteInternal(1, 1, 0x080000 | (_transfer.address & ~1u), data);
			} else {
				_mcd->WriteInternal(1, 1, 0x0C0000 | (_transfer.address & ~1u), data);
			}
		}
		break;
	}

	_transfer.address += 2;
	_transfer.source += 2;
	//length decrements twice per step (ares behavior).
	if(_transfer.length-- == 0) TransferComplete();
	if(_transfer.length-- == 0) TransferComplete();
}

//ares MCD::CDC::Transfer::read — main-CPU destination reads a word from CDC RAM.
uint16_t GenesisMcdCdc::TransferRead()
{
	if(!_transfer.ready) return 0xFFFF;
	uint16_t data = _ram[(_transfer.source >> 1) & (CdcRamWords - 1)];
	_transfer.source += 2;
	if(_transfer.length-- == 0) TransferComplete();
	if(_transfer.length-- == 0) TransferComplete();
	return data;
}

//ares MCD::CDC::Transfer::start
void GenesisMcdCdc::TransferStart()
{
	if(!_transfer.enable) return;
	_transfer.active = true;
	_transfer.busy = true;
	_transfer.ready = (_transfer.destination == 2 || _transfer.destination == 3);
	_transfer.completed = false;
	_irqTransfer.pending = false;
	Poll();
}

//ares MCD::CDC::Transfer::complete
void GenesisMcdCdc::TransferComplete()
{
	_transfer.active = false;
	_transfer.busy = false;
	_transfer.ready = false;
	_transfer.completed = true;
	_irqTransfer.pending = true;
	Poll();
}

//ares MCD::CDC::Transfer::stop
void GenesisMcdCdc::TransferStop()
{
	_transfer.active = false;
	_transfer.busy = false;
	_transfer.ready = false;
}

// ============================================================================
// ISerializable
// ============================================================================
void GenesisMcdCdc::Serialize(Serializer& s)
{
	SV(_address);
	SV(_stopwatch);

	SV(_irqDecoder.enable);  SV(_irqDecoder.pending);
	SV(_irqTransfer.enable); SV(_irqTransfer.pending);
	SV(_irqCommand.enable);  SV(_irqCommand.pending);

	for(int i = 0; i < 8; i++) SVI(_command.fifo[i]);
	SV(_command.read); SV(_command.write); SV(_command.empty);

	for(int i = 0; i < 8; i++) SVI(_status.fifo[i]);
	SV(_status.read); SV(_status.write); SV(_status.empty);
	SV(_status.enable); SV(_status.active); SV(_status.busy); SV(_status.wait);

	SV(_transfer.destination); SV(_transfer.address);
	SV(_transfer.source); SV(_transfer.target); SV(_transfer.pointer); SV(_transfer.length);
	SV(_transfer.enable); SV(_transfer.active); SV(_transfer.busy);
	SV(_transfer.wait); SV(_transfer.ready); SV(_transfer.completed);

	SV(_decoder.enable); SV(_decoder.mode); SV(_decoder.form); SV(_decoder.valid);
	SV(_header.minute); SV(_header.second); SV(_header.frame); SV(_header.mode);
	SV(_subheader.file); SV(_subheader.channel); SV(_subheader.submode); SV(_subheader.coding);

	for(size_t i = 0; i < _ram.size(); i++) SVI(_ram[i]);
}
