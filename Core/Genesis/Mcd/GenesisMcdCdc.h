#pragma once
#include "pch.h"
#include "Utilities/ISerializable.h"

class GenesisMcd;
class Serializer;

//Sanyo LC8951x CD controller (CDC), ported from ares/md/mcd/cdc.cpp +
//cdc-transfer.cpp. Handles the command/status FIFOs, the sector decoder, and
//the DMA transfer engine that moves decoded data to PRAM / WRAM / PCM / main-CPU.
//
//The CDC contributes one interrupt to the sub-CPU's IRQ controller at level 5
//(GenesisMcdIrq::cdc). Internally it has three sub-sources — decoder, transfer,
//and command — each with its own enable+pending pair. Poll() ORs the active
//sub-sources and raises/lowers the level-5 source accordingly.
//
//Register access (sub-CPU internal IO):
//  0xFF8004 lo: CDC address (5 bits)        0xFF8004 hi: transfer destination (3 bits)
//  0xFF8006 lo: CDC register read (cdc.read())   [auto-increments address]
//  0xFF8006 lo: CDC register write (cdc.write(data)) [on writes]
//  0xFF8008    : transfer data read (cdc.transfer.read())
//  0xFF800A    : transfer address (bits 3..18)
//  0xFF800C    : stopwatch (12 bits, read; writing clears)
//The CDC address auto-increments after each register read/write (except
//COMIN/SBOUT at address 0; STAT3/RESET at address 0xF wrap to 0).
class GenesisMcdCdc final : public ISerializable
{
public:
	void SetMcd(GenesisMcd* mcd) { _mcd = mcd; }

	//ares MCD::CDC::power
	void Power(bool reset);

	//ares MCD::CDC::clock — increments the stopwatch (12-bit).
	void Clock();

	//ares MCD::CDC::decode(sector). Reads the raw 2352-byte sector from the disc
	//(via GenesisMcd::ReadRawSector) and writes it into CDC RAM at the transfer
	//pointer, with the 12-byte sync header wrapped to the tail. Sets the decoder
	//IRQ sub-source if the decoder is enabled.
	void Decode(int32_t sector);

	//ares MCD::CDC::read — read the register at the current address.
	uint8_t Read();

	//ares MCD::CDC::write — write a byte to the register at the current address.
	void Write(uint8_t data);

	//ares MCD::CDC::Transfer::dma — one DMA step (called every 6 sub-CPU cycles).
	void TransferDma();

	//ares MCD::CDC::Transfer::read — read one word from CDC RAM (main-CPU dest).
	uint16_t TransferRead();

	//Recompute the level-5 IRQ line from the three sub-sources (ares poll()).
	void Poll();

	//--- register-level accessors used by the IO handlers ---
	uint8_t  GetAddress() const { return _address; }
	void     SetAddress(uint8_t a) { _address = a & 0x1F; }
	uint8_t  GetStopwatch() const { return (uint8_t)(_stopwatch & 0xFFF); }
	void     ClearStopwatch() { _stopwatch = 0; }

	uint8_t  GetTransferDestination() const { return _transfer.destination; }
	void     SetTransferDestination(uint8_t d) { _transfer.destination = d & 0x07; }
	bool     GetTransferReady() const { return _transfer.ready; }
	bool     GetTransferCompleted() const { return _transfer.completed; }

	void     SetTransferAddress(uint16_t lo) {
		//0xFF800A: bits 3..18 of the transfer address (low 3 forced to 0).
		//data.bit(0,15) → address.bit(3,18); mask 0x7FFF8 = bits 3-18.
		_transfer.address = (_transfer.address & ~0x7FFF8u) | (((uint32_t)lo & 0xFFFFu) << 3);
	}
	uint16_t GetTransferAddress() const {
		return (uint16_t)((_transfer.address >> 3) & 0xFFFFu);
	}

	//CDC RAM (16 KB = 8K words). Owned here to match ares (cdc.ram).
	static constexpr uint32_t CdcRamWords = 8 * 1024;
	std::vector<uint16_t>& GetRam() { return _ram; }
	uint16_t* GetRamData() { return _ram.data(); }

	//ISerializable
	void Serialize(Serializer& s) override;

private:
	GenesisMcd* _mcd = nullptr;
	std::vector<uint16_t> _ram;  //16 KB (8K words)

	uint8_t  _address = 0;       //5-bit register pointer
	uint16_t _stopwatch = 0;     //12-bit free-running counter

	//IRQ sub-sources (each feeds the level-5 cdc source via Poll()).
	//ares names: irq.decoder (DECEIN+DECI), irq.transfer (DTEIEN+DTEI),
	//irq.command (CMDIEN+CMDI). These are DISTINCT from the functional
	//Transfer.enable (DOUTEN) and Decoder.enable (DECEN) below.
	struct SubIRQ { bool enable = false; bool pending = false; };
	SubIRQ _irqDecoder;   //DECEIN + DECI
	SubIRQ _irqTransfer;  //DTEIEN + DTEI
	SubIRQ _irqCommand;   //CMDIEN + CMDI

	//COMIN: command input FIFO (8 bytes).
	struct Command {
		uint8_t fifo[8] = {};
		uint8_t read = 0;
		uint8_t write = 0;
		bool empty = true;
	} _command;

	//SBOUT: status byte output FIFO (8 bytes).
	struct Status {
		uint8_t fifo[8] = {};
		uint8_t read = 0;
		uint8_t write = 0;
		bool empty = true;
		bool enable = false;   //SOUTEN
		bool active = false;   //STEN
		bool busy = false;     //STBSY
		bool wait = false;     //STWAI
	} _status;

	//Transfer/DMA engine (functional state; enable here = DOUTEN).
	struct Transfer {
		uint8_t  destination = 0;   //3-bit: 2/3=mainCPU, 4=PCM, 5=PRAM, 7=WRAM
		uint32_t address = 0;       //19-bit destination address
		uint16_t source = 0;        //16-bit CDC RAM read pointer
		uint16_t target = 0;        //16-bit write address (mirror)
		uint16_t pointer = 0;       //16-bit block pointer
		uint16_t length = 0;        //12-bit byte counter

		bool enable = false;     //DOUTEN
		bool active = false;     //DTEN
		bool busy = false;       //DTBSY
		bool wait = false;       //DTWAI
		bool ready = false;      //DSR
		bool completed = false;  //EDT
	} _transfer;

	//Decoder (functional state; enable here = DECEN).
	struct Decoder {
		bool enable = false;  //DECEN
		bool mode = false;    //MODE
		bool form = false;    //FORM
		bool valid = false;   //!VALST
	} _decoder;

	struct Header {
		uint8_t minute = 0;
		uint8_t second = 0;
		uint8_t frame = 0;
		uint8_t mode = 0;
	} _header;

	struct Subheader {
		uint8_t file = 0;
		uint8_t channel = 0;
		uint8_t submode = 0;
		uint8_t coding = 0;
	} _subheader;

	struct Control {
		bool head = false;              //SHDREN
		bool mode = false;              //MODE
		bool form = false;              //FORM
		bool commandBreak = false;      //CMDBK
		bool modeByteCheck = false;     //MBCKRQ
		bool erasureRequest = false;    //ERAMRQ
		bool writeRequest = false;      //WRRQ
		bool pCodeCorrection = false;   //PRQ
		bool qCodeCorrection = false;   //QRQ
		bool autoCorrection = false;    //AUTOQ
		bool errorCorrection = false;   //E01RQ
		bool edcCorrection = false;     //EDCRQ
		bool correctionWrite = false;   //COWREN
		bool descramble = false;        //DSCREN
		bool syncDetection = false;     //SYDEN
		bool syncInterrupt = false;     //SYIEN
		bool erasureCorrection = false; //ERAMSL
		bool statusTrigger = false;     //STENTRG
		bool statusControl = false;     //STENCTL
	} _control;

	//Transfer helpers (cdc-transfer.cpp).
	void TransferStart();
	void TransferComplete();
	void TransferStop();
};
