#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"

class GenesisConsole;
class LabelManager;
class EmuSettings;
class MemoryDumper;

struct GenesisM68KOpInfo
{
	const char* Op = nullptr;
	uint8_t Size = 0; //0=byte,1=word,2=long,3=unsized
	uint8_t ByteCodeSize = 0;
};

class GenesisM68KDisUtils
{
public:
	static void GetDisassembly(DisassemblyInfo& info, string& out, uint32_t memoryAddr, LabelManager* labelManager, EmuSettings* settings);
	static uint8_t GetOpSize(uint32_t cpuAddress, MemoryType memType, MemoryDumper* memoryDumper);
	static bool IsJumpToSub(uint32_t opCode);
	static bool IsReturnInstruction(uint32_t opCode);
	static bool IsUnconditionalJump(uint32_t opCode);
	static bool IsConditionalJump(uint32_t opCode);
	static CdlFlags::CdlFlags GetOpFlags(uint32_t opCode, uint32_t pc, uint32_t prevPc);
};

class GenesisZ80DisUtils
{
public:
	static void GetDisassembly(DisassemblyInfo& info, string& out, uint32_t memoryAddr, LabelManager* labelManager, EmuSettings* settings);
	static uint8_t GetOpSize(uint8_t opCode, uint32_t cpuAddress, MemoryType memType, MemoryDumper* memoryDumper);
	static bool IsJumpToSub(uint8_t opCode);
	static bool IsReturnInstruction(uint16_t opCode);
	static bool IsUnconditionalJump(uint8_t opCode);
	static bool IsConditionalJump(uint8_t opCode);
	static CdlFlags::CdlFlags GetOpFlags(uint8_t opCode, uint16_t pc, uint16_t prevPc);
};
