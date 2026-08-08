#pragma once
#include "pch.h"

//Sega CD / Mega CD gate-array state, ported from ares/md/mcd/mcd.hpp.
//Phase A (skeleton) only needs IO/Communication/LED/Counter; the CDC/CDD/
//GPU/PCM/Timer subsystem structs are added in later phases.
//
//Reference: ares/md/mcd/mcd.hpp (structs IO, LED, Counter, Communcation).

//MCD gate-array IO registers (main-CPU + sub-CPU accessible).
//Bit widths mirror ares (n1/n2/n8/n16/n32).
struct McdIo {
	bool run = false;              //0xA12000 bit 0: sub-CPU run
	bool request = false;          //0xA12000 bit 1: bus request (1 = sub-CPU halted)
	bool halt = true;              //= request; sub-CPU held in reset/halt when 1

	uint16_t wramLatch = 0;        //one-access delay for VDP DMA from word RAM
	bool wramMode = false;         //0 = 2Mbit mode, 1 = 1Mbit mode
	bool wramSwitchRequest = false;
	bool wramSwitch = false;       //2M: 1 = sub-CPU owns WRAM, 0 = main-CPU owns
	bool wramSelect = false;       //1M: selects which 128KB bank each CPU sees
	uint8_t wramPriority = 0;      //0..3 write priority (1M dot-mapped)
	uint8_t pramBank = 0;          //0..3 PRAM bank for main-CPU 0x020000 window
	uint8_t pramProtect = 0;       //PRAM write-protect boundary (<<9)

	uint32_t vectorLevel4 = 0;     //$000070/$000072: custom level-4 vector
};

struct McdLed {
	bool red = false;
	bool green = false;
};

//Peripheral clock divider accumulators (ares MCD::step()).
//Phase A: accumulated but no peripherals are clocked yet.
struct McdCounter {
	uint32_t divider = 0;          //>= 384 -> CDC/CDD/Timer/PCM tick
	uint32_t dma = 0;              //>= 6   -> CDC transfer DMA tick
	double pcm = 0.0;              //>= freq/44100 -> CDD sample (CD-DA)
};

//8-word command + 8-word status mailboxes shared between main and sub CPU.
struct McdCommunication {
	uint8_t cfm = 0;               //main->sub flag byte (0xA1200E high byte)
	uint8_t cfs = 0;               //sub->main flag byte (0xA1200E low byte)
	uint16_t command[8] = {};      //0xA12010-0xA1201F (main writes, sub reads)
	uint16_t status[8] = {};       //0xA12020-0xA1202F (sub writes, main reads)
};
