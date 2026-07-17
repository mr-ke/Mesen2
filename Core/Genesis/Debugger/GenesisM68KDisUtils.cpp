#include "pch.h"
#include "Genesis/Debugger/GenesisM68KDisUtils.h"
#include "Genesis/GenesisConsole.h"
#include "SMS/Debugger/SmsDisUtils.h"
#include "Debugger/DisassemblyInfo.h"
#include "Debugger/LabelManager.h"
#include "Debugger/MemoryDumper.h"
#include "Shared/MemoryType.h"
#include "Shared/EmuSettings.h"
#include "Utilities/HexUtilities.h"

//--- Helpers ---

static const char* dregNames[] = { "D0","D1","D2","D3","D4","D5","D6","D7" };
static const char* aregNames[] = { "A0","A1","A2","A3","A4","A5","A6","A7" };
static const char* condCodes[] = { "T","F","HI","LS","CC","CS","NE","EQ","VC","VS","PL","MI","GE","LT","GT","LE" };

static uint16_t ReadOpWord(uint8_t* bc, int wordIndex)
{
	return (uint16_t)((bc[wordIndex * 2] << 8) | bc[wordIndex * 2 + 1]);
}

static uint32_t ReadOpLong(uint8_t* bc, int wordIndex)
{
	return ((uint32_t)bc[wordIndex * 2] << 24) | ((uint32_t)bc[wordIndex * 2 + 1] << 16) |
	       ((uint32_t)bc[wordIndex * 2 + 2] << 8) | (uint32_t)bc[wordIndex * 2 + 3];
}

// Returns the number of extension words for an effective address.
// size: 0=byte, 1=word, 2=long
static int GetEaWordCount(int mode, int reg, int size)
{
	switch(mode) {
		case 0: case 1: case 2: case 3: case 4: return 0;
		case 5: return 1; // d16(An)
		case 6: return 1; // d8(An,ix)
		case 7:
			switch(reg) {
				case 0: return 1; // (xxx).w
				case 1: return 2; // (xxx).l
				case 2: return 1; // d16(PC)
				case 3: return 1; // d8(PC,ix)
				case 4: return (size == 2) ? 2 : 1; // #imm
				default: return 0;
			}
	}
	return 0;
}

static const char* SizeSuffix(int size)
{
	switch(size) {
		case 0: return ".b";
		case 1: return ".w";
		case 2: return ".l";
		default: return "";
	}
}

// Format an effective address.  Returns the number of extension words consumed.
static int FormatEa(string& out, uint8_t* bc, int wordIndex, int mode, int reg, int size, uint32_t memoryAddr, LabelManager* labelManager)
{
	auto writeAddr = [labelManager](string& s, uint32_t addr) {
		AddressInfo addrInfo { (int32_t)addr, MemoryType::GenesisMemory };
		string label = labelManager ? labelManager->GetLabel(addrInfo) : "";
		if(label.empty()) {
			s += "$" + HexUtilities::ToHex(addr);
		} else {
			s += label;
		}
	};

	switch(mode) {
		case 0: out += dregNames[reg]; return 0;
		case 1: out += aregNames[reg]; return 0;
		case 2: out += string("(") + aregNames[reg] + ")"; return 0;
		case 3: out += string("(") + aregNames[reg] + ")+"; return 0;
		case 4: out += string("-(") + aregNames[reg] + ")"; return 0;
		case 5: {
			int16_t disp = (int16_t)ReadOpWord(bc, wordIndex);
			out += "$" + HexUtilities::ToHex((uint16_t)disp) + "(" + aregNames[reg] + ")";
			return 1;
		}
		case 6: {
			uint16_t ext = ReadOpWord(bc, wordIndex);
			int8_t disp = (int8_t)(ext & 0xFF);
			const char* xreg = (ext & 0x8000) ? aregNames[(ext >> 12) & 7] : dregNames[(ext >> 12) & 7];
			const char* xsize = (ext & 0x0800) ? ".l" : ".w";
			out += "$" + HexUtilities::ToHex((uint8_t)disp) + "(" + aregNames[reg] + "," + xreg + xsize + ")";
			return 1;
		}
		case 7:
			switch(reg) {
				case 0: {
					uint16_t addr = ReadOpWord(bc, wordIndex);
					out += "(";
					writeAddr(out, (uint32_t)(int16_t)addr);
					out += ").w";
					return 1;
				}
				case 1: {
					uint32_t addr = ReadOpLong(bc, wordIndex);
					out += "(";
					writeAddr(out, addr);
					out += ").l";
					return 2;
				}
				case 2: {
					int16_t disp = (int16_t)ReadOpWord(bc, wordIndex);
					uint32_t target = memoryAddr + 2 + disp;
					out += "(";
					writeAddr(out, target);
					out += ",pc)";
					return 1;
				}
				case 3: {
					uint16_t ext = ReadOpWord(bc, wordIndex);
					int8_t disp = (int8_t)(ext & 0xFF);
					const char* xreg = (ext & 0x8000) ? aregNames[(ext >> 12) & 7] : dregNames[(ext >> 12) & 7];
					const char* xsize = (ext & 0x0800) ? ".l" : ".w";
					out += "$" + HexUtilities::ToHex((uint8_t)disp) + "(pc," + xreg + xsize + ")";
					return 1;
				}
				case 4: {
					if(size == 2) {
						uint32_t val = ReadOpLong(bc, wordIndex);
						out += "#$" + HexUtilities::ToHex(val);
						return 2;
					} else {
						uint16_t val = ReadOpWord(bc, wordIndex);
						out += "#$" + HexUtilities::ToHex(val);
						return 1;
					}
				}
			}
			break;
	}
	return 0;
}

//--- GenesisM68KDisUtils ---

void GenesisM68KDisUtils::GetDisassembly(DisassemblyInfo& info, string& out, uint32_t memoryAddr, LabelManager* labelManager, EmuSettings* settings)
{
	uint8_t* bc = info.GetByteCode();
	uint16_t op = ReadOpWord(bc, 0);
	int nibbleHi = (op >> 12) & 0xF;

	auto writeAddr = [labelManager](uint32_t addr) -> string {
		AddressInfo addrInfo { (int32_t)addr, MemoryType::GenesisMemory };
		string label = labelManager ? labelManager->GetLabel(addrInfo) : "";
		if(label.empty()) {
			return "$" + HexUtilities::ToHex(addr);
		}
		return label;
	};

	auto writeBranchTarget = [memoryAddr, &writeAddr](int16_t disp) -> string {
		uint32_t target = memoryAddr + 2 + disp;
		return writeAddr(target);
	};

	//NOP
	if(op == 0x4E71) { out += "NOP"; return; }
	//RTS
	if(op == 0x4E75) { out += "RTS"; return; }
	//RTE
	if(op == 0x4E73) { out += "RTE"; return; }
	//RTR
	if(op == 0x4E77) { out += "RTR"; return; }
	//RESET
	if(op == 0x4E70) { out += "RESET"; return; }
	//TRAPV
	if(op == 0x4E76) { out += "TRAPV"; return; }
	//STOP #imm
	if(op == 0x4E72) { out += "STOP #$" + HexUtilities::ToHex(ReadOpWord(bc, 1)); return; }

	//TRAP #n
	if((op & 0xFFF0) == 0x4E40) {
		out += "TRAP #" + std::to_string(op & 0xF);
		return;
	}

	//LINK An,#d16
	if((op & 0xFFF8) == 0x4E50) {
		out += "LINK " + string(aregNames[op & 7]) + ",#$" + HexUtilities::ToHex(ReadOpWord(bc, 1));
		return;
	}
	//UNLK An
	if((op & 0xFFF8) == 0x4E58) {
		out += "UNLK " + string(aregNames[op & 7]);
		return;
	}
	//MOVE_TO_USP An
	if((op & 0xFFF8) == 0x4E60) {
		out += "MOVE USP," + string(aregNames[op & 7]);
		return;
	}
	//MOVE_FROM_USP An
	if((op & 0xFFF8) == 0x4E68) {
		out += "MOVE " + string(aregNames[op & 7]) + ",USP";
		return;
	}

	//JMP
	if((op & 0xFFC0) == 0x4EC0) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "JMP ";
		FormatEa(out, bc, 1, mode, reg, 0, memoryAddr, labelManager);
		return;
	}
	//JSR
	if((op & 0xFFC0) == 0x4E90) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "JSR ";
		FormatEa(out, bc, 1, mode, reg, 0, memoryAddr, labelManager);
		return;
	}

	//MOVE_FROM_SR
	if((op & 0xFFC0) == 0x40C0) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "MOVE SR,";
		FormatEa(out, bc, 1, mode, reg, 1, memoryAddr, labelManager);
		return;
	}
	//MOVE_TO_SR
	if((op & 0xFFC0) == 0x46C0) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "MOVE ";
		int n = FormatEa(out, bc, 1, mode, reg, 1, memoryAddr, labelManager);
		out += ",SR";
		return;
	}
	//MOVE_TO_CCR
	if((op & 0xFFC0) == 0x44C0) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "MOVE ";
		FormatEa(out, bc, 1, mode, reg, 0, memoryAddr, labelManager);
		out += ",CCR";
		return;
	}

	//CLR
	if((op & 0xFF00) == 0x4200) {
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "CLR" + string(SizeSuffix(size)) + " ";
		FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
		return;
	}
	//TST
	if((op & 0xFF00) == 0x4A00) {
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "TST" + string(SizeSuffix(size)) + " ";
		FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
		return;
	}
	//NEG
	if((op & 0xFF00) == 0x4400) {
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "NEG" + string(SizeSuffix(size)) + " ";
		FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
		return;
	}
	//NEGX
	if((op & 0xFF00) == 0x4000) {
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "NEGX" + string(SizeSuffix(size)) + " ";
		FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
		return;
	}
	//NOT
	if((op & 0xFF00) == 0x4600) {
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "NOT" + string(SizeSuffix(size)) + " ";
		FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
		return;
	}
	//TAS
	if((op & 0xFFC0) == 0x4AC0) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "TAS ";
		FormatEa(out, bc, 1, mode, reg, 0, memoryAddr, labelManager);
		return;
	}

	//LEA
	if((op & 0xF1C0) == 0x41C0) {
		int an = (op >> 9) & 7;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "LEA ";
		FormatEa(out, bc, 1, mode, reg, 2, memoryAddr, labelManager);
		out += "," + string(aregNames[an]);
		return;
	}
	//PEA
	if((op & 0xFFC0) == 0x4840) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "PEA ";
		FormatEa(out, bc, 1, mode, reg, 2, memoryAddr, labelManager);
		return;
	}
	//SWAP Dn
	if((op & 0xFFF8) == 0x4840) {
		out += "SWAP " + string(dregNames[op & 7]);
		return;
	}
	//EXT (word/long)
	if((op & 0xFFF8) == 0x4880 || (op & 0xFFF8) == 0x48C0) {
		const char* sz = (op & 0x40) ? ".l" : ".w";
		out += "EXT" + string(sz) + " " + string(dregNames[op & 7]);
		return;
	}

	//MOVEM
	if((op & 0xFB80) == 0x4880) {
		bool toMem = (op & 0x0400) != 0; //direction: 0=reg-to-mem, 1=mem-to-reg
		int size = (op & 0x0040) ? 2 : 1; //1=word, 0=long... actually 0=long, 1=word
		const char* sz = (op & 0x0040) ? ".w" : ".l";
		uint16_t regList = ReadOpWord(bc, 1);
		//Format register list
		string regs;
		bool first = true;
		for(int i = 0; i < 8; i++) {
			if(regList & (1 << i)) {
				if(!first) regs += "/";
				regs += dregNames[i];
				first = false;
			}
		}
		for(int i = 0; i < 8; i++) {
			if(regList & (1 << (i + 8))) {
				if(!first) regs += "/";
				regs += aregNames[i];
				first = false;
			}
		}
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "MOVEM" + string(sz) + " ";
		if(toMem) {
			out += regs + ",";
			FormatEa(out, bc, 2, mode, reg, (op & 0x0040) ? 1 : 2, memoryAddr, labelManager);
		} else {
			FormatEa(out, bc, 2, mode, reg, (op & 0x0040) ? 1 : 2, memoryAddr, labelManager);
			out += "," + regs;
		}
		return;
	}

	//MOVEQ
	if((op & 0xF100) == 0x7000) {
		int dn = (op >> 9) & 7;
		int8_t imm = (int8_t)(op & 0xFF);
		out += "MOVEQ #$" + HexUtilities::ToHex((uint8_t)imm) + "," + string(dregNames[dn]);
		return;
	}

	//BRA/BSR/Bcc
	if(nibbleHi == 0x6) {
		int cond = (op >> 8) & 0xF;
		int8_t disp8 = (int8_t)(op & 0xFF);
		string mnem;
		if(cond == 0) mnem = "BRA";
		else if(cond == 1) mnem = "BSR";
		else mnem = string("B") + condCodes[cond];

		if(disp8 != 0) {
			out += mnem + ".s " + writeBranchTarget(disp8);
		} else {
			int16_t disp16 = (int16_t)ReadOpWord(bc, 1);
			out += mnem + ".w " + writeBranchTarget(disp16);
		}
		return;
	}

	//DBcc
	if((op & 0xF0F8) == 0x50C8) {
		int cond = (op >> 8) & 0xF;
		int dn = op & 7;
		int16_t disp = (int16_t)ReadOpWord(bc, 1);
		out += "DB" + string(condCodes[cond]) + " " + string(dregNames[dn]) + "," + writeBranchTarget(disp);
		return;
	}

	//Scc
	if((op & 0xF0C0) == 0x50C0) {
		int cond = (op >> 8) & 0xF;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "S" + string(condCodes[cond]) + " ";
		FormatEa(out, bc, 1, mode, reg, 0, memoryAddr, labelManager);
		return;
	}

	//ADDQ/SUBQ
	if(nibbleHi == 0x5 && (op & 0xF0C0) != 0x50C0) {
		int data = (op >> 9) & 7;
		if(data == 0) data = 8;
		bool isSub = (op & 0x0100) != 0;
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += (isSub ? "SUBQ" : "ADDQ") + string(SizeSuffix(size)) + " #" + std::to_string(data) + ",";
		FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
		return;
	}

	//MOVE (B/W/L)
	if(nibbleHi == 0x1 || nibbleHi == 0x2 || nibbleHi == 0x3) {
		int sizeField = (op >> 12) & 3;
		int size;
		const char* sz;
		if(sizeField == 1) { size = 0; sz = ".b"; }
		else if(sizeField == 3) { size = 1; sz = ".w"; }
		else { size = 2; sz = ".l"; }

		int destReg = (op >> 9) & 7;
		int destMode = (op >> 6) & 7;
		int srcMode = (op >> 3) & 7;
		int srcReg = op & 7;

		out += "MOVE" + string(sz) + " ";
		int srcWords = FormatEa(out, bc, 1, srcMode, srcReg, size, memoryAddr, labelManager);
		out += ",";
		FormatEa(out, bc, 1 + srcWords, destMode, destReg, size, memoryAddr, labelManager);
		return;
	}

	// Immediate instructions (ORI, ANDI, SUBI, ADDI, EORI, CMPI)
	if(nibbleHi == 0x0 && (op & 0xC0) != 0x00) {
		int size = (op >> 6) & 3;
		if(size < 3) {
			const char* mnem = nullptr;
			switch(op & 0xFF00) {
				case 0x0000: mnem = "ORI"; break;
				case 0x0200: mnem = "ANDI"; break;
				case 0x0400: mnem = "SUBI"; break;
				case 0x0600: mnem = "ADDI"; break;
				case 0x0A00: mnem = "EORI"; break;
				case 0x0C00: mnem = "CMPI"; break;
			}
			if(mnem) {
				int immWords = (size == 2) ? 2 : 1;
				string immStr;
				if(size == 2) {
					uint32_t val = ReadOpLong(bc, 1);
					immStr = "#$" + HexUtilities::ToHex(val);
				} else {
					uint16_t val = ReadOpWord(bc, 1);
					immStr = "#$" + HexUtilities::ToHex(val);
				}
				int mode = (op >> 3) & 7;
				int reg = op & 7;
				out += string(mnem) + SizeSuffix(size) + " " + immStr + ",";
				FormatEa(out, bc, 1 + immWords, mode, reg, size, memoryAddr, labelManager);
				return;
			}
		}
	}

	//ORI/ANDI/EORI to CCR/SR
	if(op == 0x003C) { out += "ORI #$" + HexUtilities::ToHex(ReadOpWord(bc, 1)) + ",CCR"; return; }
	if(op == 0x007C) { out += "ORI #$" + HexUtilities::ToHex(ReadOpWord(bc, 1)) + ",SR"; return; }
	if(op == 0x023C) { out += "ANDI #$" + HexUtilities::ToHex(ReadOpWord(bc, 1)) + ",CCR"; return; }
	if(op == 0x027C) { out += "ANDI #$" + HexUtilities::ToHex(ReadOpWord(bc, 1)) + ",SR"; return; }
	if(op == 0x0A3C) { out += "EORI #$" + HexUtilities::ToHex(ReadOpWord(bc, 1)) + ",CCR"; return; }
	if(op == 0x0A7C) { out += "EORI #$" + HexUtilities::ToHex(ReadOpWord(bc, 1)) + ",SR"; return; }

	//ADD/ADDA
	if(nibbleHi == 0xD) {
		int size = (op >> 6) & 3;
		bool isAdda = (op & 0x01C0) == 0x01C0;
		if(isAdda) {
			int an = (op >> 9) & 7;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			int sz = (op & 0x0100) ? 2 : 1;
			out += "ADDA" + string(SizeSuffix(sz)) + " ";
			FormatEa(out, bc, 1, mode, reg, sz, memoryAddr, labelManager);
			out += "," + string(aregNames[an]);
		} else {
			bool toEa = (op & 0x0100) != 0;
			int dn = (op >> 9) & 7;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			out += "ADD" + string(SizeSuffix(size)) + " ";
			if(toEa) {
				out += string(dregNames[dn]) + ",";
				FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
			} else {
				FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
				out += "," + string(dregNames[dn]);
			}
		}
		return;
	}

	//SUB/SUBA
	if(nibbleHi == 0x9) {
		int size = (op >> 6) & 3;
		bool isSuba = (op & 0x01C0) == 0x01C0;
		if(isSuba) {
			int an = (op >> 9) & 7;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			int sz = (op & 0x0100) ? 2 : 1;
			out += "SUBA" + string(SizeSuffix(sz)) + " ";
			FormatEa(out, bc, 1, mode, reg, sz, memoryAddr, labelManager);
			out += "," + string(aregNames[an]);
		} else {
			bool toEa = (op & 0x0100) != 0;
			int dn = (op >> 9) & 7;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			out += "SUB" + string(SizeSuffix(size)) + " ";
			if(toEa) {
				out += string(dregNames[dn]) + ",";
				FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
			} else {
				FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
				out += "," + string(dregNames[dn]);
			}
		}
		return;
	}

	//CMP/CMPA
	if(nibbleHi == 0xB) {
		bool isCmpa = (op & 0x01C0) == 0x01C0;
		bool isCmpm = (op & 0x01C0) == 0x00C0 && ((op >> 3) & 7) == 1;
		if(isCmpa) {
			int an = (op >> 9) & 7;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			int sz = (op & 0x0100) ? 2 : 1;
			out += "CMPA" + string(SizeSuffix(sz)) + " ";
			FormatEa(out, bc, 1, mode, reg, sz, memoryAddr, labelManager);
			out += "," + string(aregNames[an]);
			return;
		} else if(isCmpm) {
			int size = (op >> 6) & 3;
			int srcReg = (op >> 0) & 7;
			int dstReg = (op >> 9) & 7;
			out += "CMPM" + string(SizeSuffix(size)) + " (A" + std::to_string(srcReg) + ")+,(A" + std::to_string(dstReg) + ")+";
			return;
		} else {
			int size = (op >> 6) & 3;
			int dn = (op >> 9) & 7;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			out += "CMP" + string(SizeSuffix(size)) + " ";
			FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
			out += "," + string(dregNames[dn]);
			return;
		}
	}

	//AND
	if(nibbleHi == 0xC) {
		int size = (op >> 6) & 3;
		bool isAdda = (op & 0x01C0) == 0x01C0;
		if(isAdda) {
			//MULU/MULS/ABCD/EXG
			if((op & 0xF0C0) == 0xC0C0) {
				int dn = (op >> 9) & 7;
				int mode = (op >> 3) & 7;
				int reg = op & 7;
				out += "MULU.W ";
				FormatEa(out, bc, 1, mode, reg, 1, memoryAddr, labelManager);
				out += "," + string(dregNames[dn]);
				return;
			}
			if((op & 0xF0C0) == 0xC1C0) {
				int dn = (op >> 9) & 7;
				int mode = (op >> 3) & 7;
				int reg = op & 7;
				out += "MULS.W ";
				FormatEa(out, bc, 1, mode, reg, 1, memoryAddr, labelManager);
				out += "," + string(dregNames[dn]);
				return;
			}
			if((op & 0xF1F0) == 0xC100) {
				int rx = (op >> 9) & 7;
				int ry = op & 7;
				out += "ABCD " + string(dregNames[ry]) + "," + string(dregNames[rx]);
				return;
			}
			if((op & 0xF1F8) == 0xC100) {
				int rx = (op >> 9) & 7;
				int ry = op & 7;
				out += "EXG " + string(dregNames[rx]) + "," + string(dregNames[ry]);
				return;
			}
			if((op & 0xF1F8) == 0xC148) {
				int rx = (op >> 9) & 7;
				int ry = op & 7;
				out += "EXG " + string(aregNames[rx]) + "," + string(aregNames[ry]);
				return;
			}
			if((op & 0xF1F8) == 0xC188) {
				int rx = (op >> 9) & 7;
				int ry = op & 7;
				out += "EXG " + string(dregNames[rx]) + "," + string(aregNames[ry]);
				return;
			}
		} else {
			bool toEa = (op & 0x0100) != 0;
			int dn = (op >> 9) & 7;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			out += "AND" + string(SizeSuffix(size)) + " ";
			if(toEa) {
				out += string(dregNames[dn]) + ",";
				FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
			} else {
				FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
				out += "," + string(dregNames[dn]);
			}
			return;
		}
	}

	//OR
	if(nibbleHi == 0x8) {
		int size = (op >> 6) & 3;
		bool isAdda = (op & 0x01C0) == 0x01C0;
		if(isAdda) {
			if((op & 0xF0C0) == 0x80C0) {
				int dn = (op >> 9) & 7;
				int mode = (op >> 3) & 7;
				int reg = op & 7;
				out += "DIVU.W ";
				FormatEa(out, bc, 1, mode, reg, 1, memoryAddr, labelManager);
				out += "," + string(dregNames[dn]);
				return;
			}
			if((op & 0xF0C0) == 0x81C0) {
				int dn = (op >> 9) & 7;
				int mode = (op >> 3) & 7;
				int reg = op & 7;
				out += "DIVS.W ";
				FormatEa(out, bc, 1, mode, reg, 1, memoryAddr, labelManager);
				out += "," + string(dregNames[dn]);
				return;
			}
			if((op & 0xF1F0) == 0x8100) {
				int rx = (op >> 9) & 7;
				int ry = op & 7;
				out += "SBCD " + string(dregNames[ry]) + "," + string(dregNames[rx]);
				return;
			}
		} else {
			bool toEa = (op & 0x0100) != 0;
			int dn = (op >> 9) & 7;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			out += "OR" + string(SizeSuffix(size)) + " ";
			if(toEa) {
				out += string(dregNames[dn]) + ",";
				FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
			} else {
				FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
				out += "," + string(dregNames[dn]);
			}
			return;
		}
	}

	//EOR
	if(nibbleHi == 0xB && (op & 0xF100) == 0xB100) {
		int size = (op >> 6) & 3;
		bool toEa = (op & 0x0100) != 0;
		int dn = (op >> 9) & 7;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		out += "EOR" + string(SizeSuffix(size)) + " " + string(dregNames[dn]) + ",";
		FormatEa(out, bc, 1, mode, reg, size, memoryAddr, labelManager);
		return;
	}

	//Shift/rotate (0xExxx)
	if(nibbleHi == 0xE) {
		int size = (op >> 6) & 3;
		int dn = (op >> 9) & 7;
		int type = (op >> 3) & 7;
		int dir = (op & 0x0100) ? 0 : 1; //0=right, 1=left
		bool isReg = !(op & 0x0020);
		const char* shiftOps[] = { "AS", "LS", "ROX", "RO" };
		const char* shiftDir = dir ? "L" : "R";

		if(isReg) {
			if(op & 0x0008) {
				//memory shift (single bit)
				out += string(shiftOps[type]) + shiftDir + " (An)";
				return;
			}
			out += string(shiftOps[type]) + shiftDir + SizeSuffix(size) + " ";
			if(op & 0x0020) {
				out += string(dregNames[dn]) + "," + string(dregNames[op & 7]);
			} else {
				out += "#" + std::to_string(dn == 0 ? 8 : dn) + "," + string(dregNames[op & 7]);
			}
		} else {
			//memory shift
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			out += string(shiftOps[type]) + shiftDir + " ";
			FormatEa(out, bc, 1, mode, reg, 0, memoryAddr, labelManager);
		}
		return;
	}

	//Fallback: unknown opcode
	out += "DC.W $" + HexUtilities::ToHex(op);
}

uint8_t GenesisM68KDisUtils::GetOpSize(uint32_t cpuAddress, MemoryType memType, MemoryDumper* memoryDumper)
{
	uint16_t op = ((uint16_t)memoryDumper->GetMemoryValue(memType, cpuAddress) << 8) |
	              memoryDumper->GetMemoryValue(memType, cpuAddress + 1);

	int nibbleHi = (op >> 12) & 0xF;

	//Single-word instructions (no extensions)
	if(op == 0x4E71 || op == 0x4E75 || op == 0x4E73 || op == 0x4E77 ||
	   op == 0x4E70 || op == 0x4E76) {
		return 2; //NOP, RTS, RTE, RTR, RESET, TRAPV
	}
	if((op & 0xFFF0) == 0x4E40) return 2; //TRAP #n
	if((op & 0xFFF8) == 0x4E58) return 2; //UNLK
	if((op & 0xFFF8) == 0x4E60) return 2; //MOVE_TO_USP
	if((op & 0xFFF8) == 0x4E68) return 2; //MOVE_FROM_USP
	if((op & 0xF1F8) == 0x4840) return 2; //SWAP
	if((op & 0xF1F8) == 0x4880) return 2; //EXT.W
	if((op & 0xF1F8) == 0x48C0) return 2; //EXT.L
	if((op & 0xF100) == 0x7000) return 2; //MOVEQ

	//STOP #imm
	if(op == 0x4E72) return 4;

	//LINK An,#d16
	if((op & 0xFFF8) == 0x4E50) return 4;

	//Branch instructions (BRA/BSR/Bcc)
	if(nibbleHi == 0x6) {
		if((op & 0xFF) == 0) return 4; //16-bit displacement
		return 2; //8-bit displacement
	}

	//DBcc
	if((op & 0xF0F8) == 0x50C8) return 4;

	//Scc
	if((op & 0xF0C0) == 0x50C0) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + GetEaWordCount(mode, reg, 0) * 2;
	}

	//ADDQ/SUBQ
	if(nibbleHi == 0x5 && (op & 0xF0C0) != 0x50C0) {
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + GetEaWordCount(mode, reg, size) * 2;
	}

	//MOVE (B/W/L)
	if(nibbleHi == 0x1 || nibbleHi == 0x2 || nibbleHi == 0x3) {
		int sizeField = (op >> 12) & 3;
		int size = (sizeField == 1) ? 0 : ((sizeField == 3) ? 1 : 2);
		int destMode = (op >> 6) & 7;
		int destReg = (op >> 9) & 7;
		int srcMode = (op >> 3) & 7;
		int srcReg = op & 7;
		int extWords = GetEaWordCount(srcMode, srcReg, size) + GetEaWordCount(destMode, destReg, size);
		return 2 + extWords * 2;
	}

	//JMP/JSR
	if((op & 0xFFC0) == 0x4EC0 || (op & 0xFFC0) == 0x4E90) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + GetEaWordCount(mode, reg, 0) * 2;
	}

	//LEA
	if((op & 0xF1C0) == 0x41C0) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + GetEaWordCount(mode, reg, 2) * 2;
	}

	//PEA
	if((op & 0xFFC0) == 0x4840) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + GetEaWordCount(mode, reg, 2) * 2;
	}

	//CLR/TST/NEG/NEGX/NOT/TAS
	if((op & 0xFF00) == 0x4200 || (op & 0xFF00) == 0x4A00 ||
	   (op & 0xFF00) == 0x4400 || (op & 0xFF00) == 0x4000 ||
	   (op & 0xFF00) == 0x4600 || (op & 0xFFC0) == 0x4AC0) {
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + GetEaWordCount(mode, reg, size) * 2;
	}

	//MOVE_FROM_SR / MOVE_TO_CCR / MOVE_TO_SR
	if((op & 0xFFC0) == 0x40C0 || (op & 0xFFC0) == 0x44C0 || (op & 0xFFC0) == 0x46C0) {
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + GetEaWordCount(mode, reg, 1) * 2;
	}

	//MOVEM
	if((op & 0xFB80) == 0x4880) {
		int size = (op & 0x0040) ? 1 : 2;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 4 + GetEaWordCount(mode, reg, size) * 2;
	}

	//Immediate instructions (ORI/ANDI/SUBI/ADDI/EORI/CMPI)
	if(nibbleHi == 0x0 && ((op >> 6) & 3) < 3) {
		int size = (op >> 6) & 3;
		int immWords = (size == 2) ? 2 : 1;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + immWords * 2 + GetEaWordCount(mode, reg, size) * 2;
	}
	//ORI/ANDI/EORI to CCR/SR
	if(op == 0x003C || op == 0x007C || op == 0x023C || op == 0x027C ||
	   op == 0x0A3C || op == 0x0A7C) {
		return 4;
	}

	//ADD/ADDA, SUB/SUBA
	if(nibbleHi == 0xD || nibbleHi == 0x9) {
		bool isAddrA = (op & 0x01C0) == 0x01C0;
		if(isAddrA) {
			int sz = (op & 0x0100) ? 2 : 1;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			return 2 + GetEaWordCount(mode, reg, sz) * 2;
		} else {
			int size = (op >> 6) & 3;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			return 2 + GetEaWordCount(mode, reg, size) * 2;
		}
	}

	//CMP/CMPA/CMPM
	if(nibbleHi == 0xB) {
		bool isCmpa = (op & 0x01C0) == 0x01C0;
		if(isCmpa) {
			int sz = (op & 0x0100) ? 2 : 1;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			return 2 + GetEaWordCount(mode, reg, sz) * 2;
		} else {
			int size = (op >> 6) & 3;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			return 2 + GetEaWordCount(mode, reg, size) * 2;
		}
	}

	//AND/MULU/MULS/ABCD/EXG
	if(nibbleHi == 0xC) {
		bool isSpecial = (op & 0x01C0) == 0x01C0;
		if(isSpecial) {
			if((op & 0xF0C0) == 0xC0C0 || (op & 0xF0C0) == 0xC1C0) {
				//MULU/MULS
				int mode = (op >> 3) & 7;
				int reg = op & 7;
				return 2 + GetEaWordCount(mode, reg, 1) * 2;
			}
			return 2; //ABCD, EXG
		} else {
			int size = (op >> 6) & 3;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			return 2 + GetEaWordCount(mode, reg, size) * 2;
		}
	}

	//OR/DIVU/DIVS/SBCD
	if(nibbleHi == 0x8) {
		bool isSpecial = (op & 0x01C0) == 0x01C0;
		if(isSpecial) {
			if((op & 0xF0C0) == 0x80C0 || (op & 0xF0C0) == 0x81C0) {
				//DIVU/DIVS
				int mode = (op >> 3) & 7;
				int reg = op & 7;
				return 2 + GetEaWordCount(mode, reg, 1) * 2;
			}
			return 2; //SBCD
		} else {
			int size = (op >> 6) & 3;
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			return 2 + GetEaWordCount(mode, reg, size) * 2;
		}
	}

	//EOR
	if(nibbleHi == 0xB && (op & 0xF100) == 0xB100) {
		int size = (op >> 6) & 3;
		int mode = (op >> 3) & 7;
		int reg = op & 7;
		return 2 + GetEaWordCount(mode, reg, size) * 2;
	}

	//Shift/rotate
	if(nibbleHi == 0xE) {
		if(!(op & 0x0008)) {
			//Register shift
			return 2;
		} else {
			//Memory shift
			int mode = (op >> 3) & 7;
			int reg = op & 7;
			return 2 + GetEaWordCount(mode, reg, 0) * 2;
		}
	}

	//Default: assume 2 bytes (single word opcode)
	return 2;
}

bool GenesisM68KDisUtils::IsJumpToSub(uint32_t opCode)
{
	uint16_t op = (uint16_t)opCode;
	//BSR
	if((op & 0xFF00) == 0x6100) return true;
	//JSR
	if((op & 0xFFC0) == 0x4E90) return true;
	return false;
}

bool GenesisM68KDisUtils::IsReturnInstruction(uint32_t opCode)
{
	uint16_t op = (uint16_t)opCode;
	//RTS, RTE, RTR
	if(op == 0x4E75 || op == 0x4E73 || op == 0x4E77) return true;
	return false;
}

bool GenesisM68KDisUtils::IsUnconditionalJump(uint32_t opCode)
{
	uint16_t op = (uint16_t)opCode;
	//BRA
	if((op & 0xFF00) == 0x6000) return true;
	//BSR
	if((op & 0xFF00) == 0x6100) return true;
	//JMP
	if((op & 0xFFC0) == 0x4EC0) return true;
	//JSR
	if((op & 0xFFC0) == 0x4E90) return true;
	return false;
}

bool GenesisM68KDisUtils::IsConditionalJump(uint32_t opCode)
{
	uint16_t op = (uint16_t)opCode;
	//Bcc (excluding BRA=0x60 and BSR=0x61)
	if((op & 0xF000) == 0x6000 && (op & 0x0F00) >= 0x0200) return true;
	//DBcc
	if((op & 0xF0F8) == 0x50C8) return true;
	return false;
}

CdlFlags::CdlFlags GenesisM68KDisUtils::GetOpFlags(uint32_t opCode, uint32_t pc, uint32_t prevPc)
{
	return CdlFlags::None;
}


//--- GenesisZ80DisUtils (delegates to SmsDisUtils - same Z80 CPU) ---

void GenesisZ80DisUtils::GetDisassembly(DisassemblyInfo& info, string& out, uint32_t memoryAddr, LabelManager* labelManager, EmuSettings* settings)
{
	SmsDisUtils::GetDisassembly(info, out, memoryAddr, labelManager, settings);
}

uint8_t GenesisZ80DisUtils::GetOpSize(uint8_t opCode, uint32_t cpuAddress, MemoryType memType, MemoryDumper* memoryDumper)
{
	return SmsDisUtils::GetOpSize(opCode, cpuAddress, memType, memoryDumper);
}

bool GenesisZ80DisUtils::IsJumpToSub(uint8_t opCode)
{
	return SmsDisUtils::IsJumpToSub(opCode);
}

bool GenesisZ80DisUtils::IsReturnInstruction(uint16_t opCode)
{
	return SmsDisUtils::IsReturnInstruction(opCode);
}

bool GenesisZ80DisUtils::IsUnconditionalJump(uint8_t opCode)
{
	return SmsDisUtils::IsUnconditionalJump(opCode);
}

bool GenesisZ80DisUtils::IsConditionalJump(uint8_t opCode)
{
	return SmsDisUtils::IsConditionalJump(opCode);
}

CdlFlags::CdlFlags GenesisZ80DisUtils::GetOpFlags(uint8_t opCode, uint16_t pc, uint16_t prevPc)
{
	return SmsDisUtils::GetOpFlags(opCode, pc, prevPc);
}
