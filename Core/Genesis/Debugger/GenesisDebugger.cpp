#include "pch.h"
#include "Genesis/Debugger/GenesisDebugger.h"
#include "Genesis/Debugger/GenesisAssembler.h"
#include "Genesis/Debugger/GenesisM68KDisUtils.h"
#include "Genesis/Debugger/GenesisEventManager.h"
#include "Genesis/Debugger/GenesisTraceLogger.h"
#include "Genesis/Debugger/GenesisVdpTools.h"
#include "Genesis/GenesisConsole.h"
#include "Genesis/GenesisM68K.h"
#include "Genesis/GenesisZ80.h"
#include "Genesis/GenesisVdp.h"
#include "Genesis/GenesisMemoryManager.h"
#include "Genesis/GenesisTypes.h"
#include "Debugger/DisassemblyInfo.h"
#include "Debugger/Disassembler.h"
#include "Debugger/CallstackManager.h"
#include "Debugger/BreakpointManager.h"
#include "Debugger/Debugger.h"
#include "Debugger/MemoryAccessCounter.h"
#include "Debugger/ExpressionEvaluator.h"
#include "Debugger/MemoryDumper.h"
#include "Debugger/CodeDataLogger.h"
#include "Debugger/BaseEventManager.h"
#include "Debugger/StepBackManager.h"
#include "Utilities/HexUtilities.h"
#include "Utilities/Patches/IpsPatcher.h"
#include "Shared/EmuSettings.h"
#include "Shared/Emulator.h"
#include "Shared/MemoryOperationType.h"

GenesisDebugger::GenesisDebugger(Debugger* debugger, CpuType cpuType) : IDebugger(debugger->GetEmulator())
{
	_debugger = debugger;
	_emu = debugger->GetEmulator();
	_cpuType = cpuType;
	_disassembler = debugger->GetDisassembler();
	_memoryAccessCounter = debugger->GetMemoryAccessCounter();
	_settings = debugger->GetEmulator()->GetSettings();

	_console = (GenesisConsole*)debugger->GetConsole();
	_m68k = _console->GetM68K();
	_z80 = _console->GetZ80();
	_vdp = _console->GetVdp();
	_memoryManager = _console->GetMemoryManager();

	MemoryType romType = MemoryType::GenesisCartridgeRom;
	auto memInfo = _emu->GetMemory(romType);
	_codeDataLogger.reset(new CodeDataLogger(debugger, romType, memInfo.Size, _cpuType, _emu->GetCrc32()));
	_cdlFile = _codeDataLogger->GetCdlFilePath(_emu->GetRomInfo().RomFile.GetFileName());
	_codeDataLogger->LoadCdlFile(_cdlFile, _settings->GetDebugConfig().AutoResetCdl);

	if(_cpuType == CpuType::GenesisM68K) {
		_traceLogger.reset(new GenesisM68KTraceLogger(debugger, this, _vdp));
	} else {
		_traceLogger.reset(new GenesisZ80TraceLogger(debugger, this, _vdp));
	}

	_ppuTools.reset(new GenesisVdpTools(debugger, debugger->GetEmulator(), _console));
	_stepBackManager.reset(new StepBackManager(_emu, this));
	_eventManager.reset(new GenesisEventManager(debugger, _console, _vdp, _cpuType));
	_callstackManager.reset(new CallstackManager(debugger, this));
	_breakpointManager.reset(new BreakpointManager(debugger, this, _cpuType, _eventManager.get()));
	_step.reset(new StepRequest());

	if(_cpuType == CpuType::GenesisM68K) {
		_assembler.reset(new GenesisM68KAssembler(debugger->GetLabelManager()));
	} else {
		_assembler.reset(new GenesisZ80Assembler(debugger->GetLabelManager()));
	}
}

GenesisDebugger::~GenesisDebugger()
{
	_codeDataLogger->SaveCdlFile(_cdlFile);
}

void GenesisDebugger::LogNonExec(MemoryOperationInfo& operation, AddressInfo& addressInfo)
{
	if(_cpuType == CpuType::GenesisM68K) {
		((GenesisM68KTraceLogger*)_traceLogger.get())->LogNonExec(operation, addressInfo);
	} else {
		((GenesisZ80TraceLogger*)_traceLogger.get())->LogNonExec(operation, addressInfo);
	}
}

void GenesisDebugger::OnBeforeBreak(CpuType cpuType)
{
}

void GenesisDebugger::Reset()
{
	_callstackManager->Clear();
	ResetPrevOpCode();
}

void GenesisDebugger::ProcessInstruction()
{
	uint32_t pc = GetProgramCounter(false);
	AddressInfo relAddr { (int32_t)pc, MemoryType::None };
	AddressInfo addressInfo = _console->GetAbsoluteAddress(relAddr);

	if(addressInfo.Address >= 0 && addressInfo.Type == MemoryType::GenesisCartridgeRom) {
		_codeDataLogger->SetCode(addressInfo.Address);
	}
	_disassembler->BuildCache(addressInfo, 0, _cpuType);

	InstructionProgress.StartCycle = GetCpuCycleCount(false);

	_prevProgramCounter = pc;
	_prevOpCode = 0;

	if(_cpuType == CpuType::GenesisM68K) {
		_prevStackPointer = _m68k->GetAddrReg(7);
	} else {
		_prevStackPointer = _z80->GetSP();
	}

	_step->ProcessCpuExec();

	MemoryOperationInfo operation(pc, 0, MemoryOperationType::ExecOpCode, _cpuType == CpuType::GenesisM68K ? MemoryType::GenesisMemory : MemoryType::GenesisZ80Bus);
	InstructionProgress.LastMemOperation = operation;

	_debugger->ProcessBreakConditions(_cpuType, *_step.get(), _breakpointManager.get(), operation, addressInfo);
}

void GenesisDebugger::ProcessRead(uint32_t addr, uint8_t value, MemoryOperationType type)
{
	AddressInfo relAddr { (int32_t)addr, MemoryType::None };
	AddressInfo addressInfo = _console->GetAbsoluteAddress(relAddr);
	MemoryType memType = (_cpuType == CpuType::GenesisM68K) ? MemoryType::GenesisMemory : MemoryType::GenesisZ80Bus;
	MemoryOperationInfo operation(addr, value, type, memType);
	InstructionProgress.LastMemOperation = operation;

	if(type == MemoryOperationType::ExecOpCode) {
		if(_traceLogger->IsEnabled()) {
			DisassemblyInfo disInfo = _disassembler->GetDisassemblyInfo(addressInfo, addr, 0, _cpuType);
			if(_cpuType == CpuType::GenesisM68K) {
				GenesisM68KState state = {};
				state.PC = addr;
				((GenesisM68KTraceLogger*)_traceLogger.get())->Log(state, disInfo, operation, addressInfo);
			} else {
				GenesisZ80State state = {};
				state.PC = (uint16_t)addr;
				((GenesisZ80TraceLogger*)_traceLogger.get())->Log(state, disInfo, operation, addressInfo);
			}
		}
		_memoryAccessCounter->ProcessMemoryExec(addressInfo, _console->GetMasterClock());
		if(_step->ProcessCpuCycle()) {
			_debugger->SleepUntilResume(_cpuType, BreakSource::CpuStep, &operation);
		}
	} else if(type == MemoryOperationType::ExecOperand) {
		if(addressInfo.Address >= 0 && addressInfo.Type == MemoryType::GenesisCartridgeRom) {
			_codeDataLogger->SetCode(addressInfo.Address);
		}
		if(_traceLogger->IsEnabled()) {
			LogNonExec(operation, addressInfo);
		}
		_memoryAccessCounter->ProcessMemoryExec(addressInfo, _console->GetMasterClock());
		_step->ProcessCpuCycle();
	} else {
		if(addressInfo.Address >= 0 && addressInfo.Type == MemoryType::GenesisCartridgeRom) {
			_codeDataLogger->SetData(addressInfo.Address);
		}
		if(_traceLogger->IsEnabled()) {
			LogNonExec(operation, addressInfo);
		}
		_memoryAccessCounter->ProcessMemoryRead(addressInfo, _console->GetMasterClock());
	}

	_step->ProcessCpuCycle();
	_debugger->ProcessBreakConditions(_cpuType, *_step.get(), _breakpointManager.get(), operation, addressInfo);
}

void GenesisDebugger::ProcessWrite(uint32_t addr, uint8_t value, MemoryOperationType type)
{
	AddressInfo relAddr { (int32_t)addr, MemoryType::None };
	AddressInfo addressInfo = _console->GetAbsoluteAddress(relAddr);
	MemoryType memType = (_cpuType == CpuType::GenesisM68K) ? MemoryType::GenesisMemory : MemoryType::GenesisZ80Bus;
	MemoryOperationInfo operation(addr, value, type, memType);
	InstructionProgress.LastMemOperation = operation;

	if(addressInfo.Type == MemoryType::GenesisM68KRam || addressInfo.Type == MemoryType::GenesisZ80Ram || addressInfo.Type == MemoryType::GenesisCartridgeRam) {
		_disassembler->InvalidateCache(addressInfo, _cpuType);
	}

	if(_traceLogger->IsEnabled()) {
		LogNonExec(operation, addressInfo);
	}

	_memoryAccessCounter->ProcessMemoryWrite(addressInfo, _console->GetMasterClock());
	_step->ProcessCpuCycle();
	_debugger->ProcessBreakConditions(_cpuType, *_step.get(), _breakpointManager.get(), operation, addressInfo);
}

template<MemoryOperationType opType>
void GenesisDebugger::ProcessMemoryAccess(uint32_t addr, uint8_t value, MemoryType memType)
{
	MemoryOperationInfo operation(addr, value, opType, memType);
	_eventManager->AddEvent(DebugEventType::Register, operation);
}

void GenesisDebugger::ProcessInterrupt(uint32_t originalPc, uint32_t currentPc, bool forNmi)
{
	AddressInfo relRet { (int32_t)originalPc, MemoryType::None };
	AddressInfo ret = _console->GetAbsoluteAddress(relRet);
	AddressInfo relDest { (int32_t)currentPc, MemoryType::None };
	AddressInfo dest = _console->GetAbsoluteAddress(relDest);

	if(dest.Type == MemoryType::GenesisCartridgeRom && dest.Address >= 0) {
		_codeDataLogger->SetCode(dest.Address, CdlFlags::SubEntryPoint);
	}

	uint32_t originalSp = (_cpuType == CpuType::GenesisM68K) ? _m68k->GetAddrReg(7) : _z80->GetSP();
	_prevStackPointer = originalSp;

	ResetPrevOpCode();

	_debugger->InternalProcessInterrupt(
		_cpuType, *this, *_step.get(),
		ret, originalPc, dest, currentPc, ret, originalPc, originalSp, forNmi
	);
}

void GenesisDebugger::ProcessPpuRead(uint16_t addr, uint8_t value, MemoryType memoryType)
{
	MemoryOperationInfo operation(addr, value, MemoryOperationType::Read, memoryType);
	AddressInfo addressInfo { addr, memoryType };
	_debugger->ProcessBreakConditions(_cpuType, *_step.get(), _breakpointManager.get(), operation, addressInfo);
	_memoryAccessCounter->ProcessMemoryRead(addressInfo, _console->GetMasterClock());
}

void GenesisDebugger::ProcessPpuWrite(uint16_t addr, uint8_t value, MemoryType memoryType)
{
	MemoryOperationInfo operation(addr, value, MemoryOperationType::Write, memoryType);
	AddressInfo addressInfo { addr, memoryType };
	_debugger->ProcessBreakConditions(_cpuType, *_step.get(), _breakpointManager.get(), operation, addressInfo);
	_memoryAccessCounter->ProcessMemoryWrite(addressInfo, _console->GetMasterClock());
}

void GenesisDebugger::ProcessPpuCycle()
{
	if(_ppuTools->HasOpenedViewer()) {
		_ppuTools->UpdateViewers(_vdp->GetVCounter(), _vdp->GetHCounter());
	}

	if(_step->HasRequest) {
		if(_step->HasScanlineBreakRequest() && _vdp->GetHCounter() == 0 && _vdp->GetVCounter() == _step->BreakScanline) {
			_debugger->SleepUntilResume(_cpuType, _step->GetBreakSource());
		} else if(_step->PpuStepCount > 0) {
			_step->PpuStepCount--;
			if(_step->PpuStepCount == 0) {
				_debugger->SleepUntilResume(_cpuType, _step->GetBreakSource());
			}
		}
	}
}

void GenesisDebugger::Run()
{
	_step.reset(new StepRequest());
}

void GenesisDebugger::Step(int32_t stepCount, StepType type)
{
	StepRequest step(type);

	switch(type) {
		case StepType::Step: step.StepCount = stepCount; break;
		case StepType::StepOut:
			step.BreakAddress = _callstackManager->GetReturnAddress();
			step.BreakStackPointer = _callstackManager->GetReturnStackPointer();
			break;

		case StepType::StepOver:
			if(_cpuType == CpuType::GenesisM68K) {
				//M68K: BSR, JSR are subroutine calls
				//For simplicity, step over = step into for now
				step.StepCount = 1;
			} else {
				//Z80: CALL is subroutine call
				step.StepCount = 1;
			}
			break;

		case StepType::CpuCycleStep: step.CpuCycleStepCount = stepCount; break;
		case StepType::PpuStep: step.PpuStepCount = stepCount; break;
		case StepType::PpuScanline: step.PpuStepCount = 488 * stepCount; break;
		case StepType::PpuFrame: step.PpuStepCount = 488 * _vdp->GetScreenHeight() * stepCount; break;
		case StepType::SpecificScanline: step.BreakScanline = stepCount; break;
	}

	_step.reset(new StepRequest(step));
}

StepBackConfig GenesisDebugger::GetStepBackConfig()
{
	return {
		GetCpuCycleCount(),
		488 * 2,
		488u * 2 * _vdp->GetScreenHeight()
	};
}

void GenesisDebugger::DrawPartialFrame()
{
}

void GenesisDebugger::SetProgramCounter(uint32_t addr, bool updateDebuggerOnly)
{
	if(!updateDebuggerOnly) {
		if(_cpuType == CpuType::GenesisM68K) {
			_m68k->SetPC(addr);
		} else {
			_z80->SetPC((uint16_t)addr);
		}
	}
	_prevProgramCounter = addr;
}

uint32_t GenesisDebugger::GetProgramCounter(bool getInstPc)
{
	if(getInstPc) {
		return _prevProgramCounter;
	}
	if(_cpuType == CpuType::GenesisM68K) {
		return _m68k->GetPC();
	} else {
		return _z80->GetPC();
	}
}

uint64_t GenesisDebugger::GetCpuCycleCount(bool forProfiler)
{
	return _console->GetMasterClock();
}

void GenesisDebugger::ResetPrevOpCode()
{
	_prevOpCode = 0;
}

DebuggerFeatures GenesisDebugger::GetSupportedFeatures()
{
	DebuggerFeatures features = {};
	features.RunToIrq = true;
	features.RunToNmi = true;
	features.StepOver = true;
	features.StepOut = true;
	features.StepBack = true;
	features.CallStack = true;
	features.CpuCycleStep = true;
	features.ChangeProgramCounter = AllowChangeProgramCounter;

	if(_cpuType == CpuType::GenesisM68K) {
		//M68K exception vectors
		features.CpuVectors[0] = { "IRQ Level 1", 0x64, VectorType::Direct };
		features.CpuVectors[1] = { "IRQ Level 2", 0x68, VectorType::Direct };
		features.CpuVectors[2] = { "IRQ Level 3", 0x6C, VectorType::Direct };
		features.CpuVectors[3] = { "IRQ L4 HBlank", 0x70, VectorType::Direct };
		features.CpuVectors[4] = { "IRQ Level 5", 0x74, VectorType::Direct };
		features.CpuVectors[5] = { "IRQ L6 VBlank", 0x78, VectorType::Direct };
		features.CpuVectors[6] = { "IRQ Level 7", 0x7C, VectorType::Direct };
		features.CpuVectorCount = 7;
	} else {
		//Z80 interrupt mode 1 vector
		features.CpuVectors[0] = { "IRQ", 0x38, VectorType::Direct };
		features.CpuVectors[1] = { "NMI", 0x66, VectorType::Direct };
		features.CpuVectorCount = 2;
	}

	return features;
}

BaseEventManager* GenesisDebugger::GetEventManager()
{
	return _eventManager.get();
}

IAssembler* GenesisDebugger::GetAssembler()
{
	return _assembler.get();
}

CallstackManager* GenesisDebugger::GetCallstackManager()
{
	return _callstackManager.get();
}

BreakpointManager* GenesisDebugger::GetBreakpointManager()
{
	return _breakpointManager.get();
}

ITraceLogger* GenesisDebugger::GetTraceLogger()
{
	return _traceLogger.get();
}

PpuTools* GenesisDebugger::GetPpuTools()
{
	return _ppuTools.get();
}

BaseState& GenesisDebugger::GetState()
{
	if(_cpuType == CpuType::GenesisM68K) {
		static GenesisM68KState m68kState;
		m68kState = {};
		for(int i = 0; i < 8; i++) {
			m68kState.D[i] = _m68k->GetDataReg(i);
			m68kState.A[i] = _m68k->GetAddrReg(i);
		}
		m68kState.PC = _m68k->GetPC();
		m68kState.SR = _m68k->GetSR();
		m68kState.SSP = _m68k->GetAddrReg(7);
		m68kState.Stopped = _m68k->IsStopped();
		return m68kState;
	} else {
		static GenesisZ80State z80State;
		z80State = {};
		z80State.A = _z80->GetA();
		z80State.Flags = _z80->GetFlags();
		z80State.B = _z80->GetB();
		z80State.C = _z80->GetC();
		z80State.D = _z80->GetD();
		z80State.E = _z80->GetE();
		z80State.H = _z80->GetH();
		z80State.L = _z80->GetL();
		z80State.IX = _z80->GetIX();
		z80State.IY = _z80->GetIY();
		z80State.SP = _z80->GetSP();
		z80State.PC = _z80->GetPC();
		z80State.I = _z80->GetI();
		z80State.R = _z80->GetR();
		z80State.Halted = _z80->IsHalted();
		return z80State;
	}
}

void GenesisDebugger::GetPpuState(BaseState& state)
{
	(GenesisVdpState&)state = {};
	GenesisVdpState& vdpState = (GenesisVdpState&)state;
	vdpState.VCounter = _vdp->GetVCounter();
	vdpState.HCounter = _vdp->GetHCounter();
	vdpState.VBlank = _vdp->IsVblank();
	vdpState.HBlank = _vdp->IsHblank();
	vdpState.DisplayEnable = _vdp->IsDisplayEnable();
	for(uint32_t i = 0; i < 64; i++) {
		vdpState.Cram[i] = _vdp->DebugReadCRAM(i);
	}
}

void GenesisDebugger::SetPpuState(BaseState& srcState)
{
	//Read-only for now - VDP state restoration not implemented
}

bool GenesisDebugger::SaveRomToDisk(string filename, bool saveAsIps, CdlStripOption stripOption)
{
	uint8_t* prgRom = _debugger->GetMemoryDumper()->GetMemoryBuffer(MemoryType::GenesisCartridgeRom);
	uint32_t prgRomSize = _debugger->GetMemoryDumper()->GetMemorySize(MemoryType::GenesisCartridgeRom);
	if(!prgRom || prgRomSize == 0) return false;

	vector<uint8_t> rom = vector<uint8_t>(prgRom, prgRom + prgRomSize);

	vector<uint8_t> output;
	if(saveAsIps) {
		vector<uint8_t> originalRom;
		_emu->GetRomInfo().RomFile.ReadFile(originalRom);
		output = IpsPatcher::CreatePatch(originalRom, rom);
	} else {
		if(stripOption != CdlStripOption::StripNone) {
			_codeDataLogger->StripData(rom.data(), stripOption);
		}
		output = rom;
	}

	ofstream file(filename, ios::out | ios::binary);
	if(file) {
		file.write((char*)output.data(), output.size());
		file.close();
		return true;
	}
	return false;
}

template void GenesisDebugger::ProcessMemoryAccess<MemoryOperationType::Read>(uint32_t addr, uint8_t value, MemoryType memType);
template void GenesisDebugger::ProcessMemoryAccess<MemoryOperationType::Write>(uint32_t addr, uint8_t value, MemoryType memType);
