#include "pch.h"
#include "Genesis/Debugger/GenesisAssembler.h"
#include "Debugger/LabelManager.h"

GenesisM68KAssembler::GenesisM68KAssembler(LabelManager* labelManager)
{
	_labelManager = labelManager;
}

GenesisM68KAssembler::~GenesisM68KAssembler()
{
}

uint32_t GenesisM68KAssembler::AssembleCode(string code, uint32_t startAddress, int16_t* assembledCode)
{
	//Stub implementation - assembler support not yet available
	return 0;
}

GenesisZ80Assembler::GenesisZ80Assembler(LabelManager* labelManager)
{
	_labelManager = labelManager;
}

GenesisZ80Assembler::~GenesisZ80Assembler()
{
}

uint32_t GenesisZ80Assembler::AssembleCode(string code, uint32_t startAddress, int16_t* assembledCode)
{
	//Stub implementation - assembler support not yet available
	return 0;
}
