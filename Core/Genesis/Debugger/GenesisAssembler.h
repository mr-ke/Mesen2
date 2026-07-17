#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/IAssembler.h"

class LabelManager;

class GenesisM68KAssembler : public IAssembler
{
	LabelManager* _labelManager;

public:
	GenesisM68KAssembler(LabelManager* labelManager);
	~GenesisM68KAssembler();

	uint32_t AssembleCode(string code, uint32_t startAddress, int16_t* assembledCode) override;
};

class GenesisZ80Assembler : public IAssembler
{
	LabelManager* _labelManager;

public:
	GenesisZ80Assembler(LabelManager* labelManager);
	~GenesisZ80Assembler();

	uint32_t AssembleCode(string code, uint32_t startAddress, int16_t* assembledCode) override;
};
