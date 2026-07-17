#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/IDebugger.h"

class Disassembler;
class Debugger;
class GenesisTraceLogger;
class GenesisConsole;
class GenesisMemoryManager;
class GenesisM68K;
class GenesisZ80;
class GenesisVdp;
class CallstackManager;
class MemoryAccessCounter;
class BreakpointManager;
class EmuSettings;
class IAssembler;
class ITraceLogger;
class GenesisEventManager;
class GenesisVdpTools;
class Emulator;
class CodeDataLogger;

enum class MemoryOperationType;

class GenesisDebugger final : public IDebugger
{
	Debugger* _debugger;
	Emulator* _emu;
	CpuType _cpuType;
	GenesisConsole* _console;
	GenesisMemoryManager* _memoryManager;
	GenesisM68K* _m68k;
	GenesisZ80* _z80;
	GenesisVdp* _vdp;
	Disassembler* _disassembler;
	MemoryAccessCounter* _memoryAccessCounter;
	EmuSettings* _settings;

	unique_ptr<GenesisEventManager> _eventManager;
	unique_ptr<GenesisVdpTools> _ppuTools;
	unique_ptr<CallstackManager> _callstackManager;
	unique_ptr<CodeDataLogger> _codeDataLogger;
	unique_ptr<BreakpointManager> _breakpointManager;
	unique_ptr<IAssembler> _assembler;
	unique_ptr<ITraceLogger> _traceLogger;

	uint32_t _prevOpCode = 0;
	uint32_t _prevProgramCounter = 0;
	uint32_t _prevStackPointer = 0;

	string _cdlFile;

	void LogNonExec(MemoryOperationInfo& operation, AddressInfo& addressInfo);

public:
	GenesisDebugger(Debugger* debugger, CpuType cpuType);
	~GenesisDebugger();

	void OnBeforeBreak(CpuType cpuType) override;
	void Reset() override;

	void ProcessInstruction();
	void ProcessRead(uint32_t addr, uint8_t value, MemoryOperationType type);
	void ProcessWrite(uint32_t addr, uint8_t value, MemoryOperationType type);

	template<MemoryOperationType opType>
	void ProcessMemoryAccess(uint32_t addr, uint8_t value, MemoryType memType);

	void ProcessInterrupt(uint32_t originalPc, uint32_t currentPc, bool forNmi) override;
	void ProcessPpuRead(uint16_t addr, uint8_t value, MemoryType memoryType);
	void ProcessPpuWrite(uint16_t addr, uint8_t value, MemoryType memoryType);
	void ProcessPpuCycle();

	void Run() override;
	void Step(int32_t stepCount, StepType type) override;
	StepBackConfig GetStepBackConfig() override;

	void DrawPartialFrame() override;

	void SetProgramCounter(uint32_t addr, bool updateDebuggerOnly = false) override;
	uint32_t GetProgramCounter(bool getInstPc) override;
	uint64_t GetCpuCycleCount(bool forProfiler = false) override;
	void ResetPrevOpCode() override;

	DebuggerFeatures GetSupportedFeatures() override;

	BaseEventManager* GetEventManager() override;
	IAssembler* GetAssembler() override;
	CallstackManager* GetCallstackManager() override;
	BreakpointManager* GetBreakpointManager() override;
	ITraceLogger* GetTraceLogger() override;
	PpuTools* GetPpuTools() override;

	BaseState& GetState() override;
	void GetPpuState(BaseState& state) override;
	void SetPpuState(BaseState& state) override;

	bool SaveRomToDisk(string filename, bool saveAsIps, CdlStripOption stripOption);
};
