#pragma once
#include "pch.h"
#include "Debugger/BaseTraceLogger.h"
#include "Genesis/GenesisTypes.h"

class DisassemblyInfo;
class Debugger;
class GenesisVdp;

class GenesisM68KTraceLogger : public BaseTraceLogger<GenesisM68KTraceLogger, GenesisM68KState>
{
private:
	GenesisVdp* _vdp = nullptr;

protected:
	RowDataType GetFormatTagType(string& tag) override;

public:
	GenesisM68KTraceLogger(Debugger* debugger, IDebugger* cpuDebugger, GenesisVdp* vdp);

	void GetTraceRow(string& output, GenesisM68KState& cpuState, TraceLogPpuState& vdpState, DisassemblyInfo& disassemblyInfo);
	void LogPpuState();

	__forceinline uint32_t GetProgramCounter(GenesisM68KState& state) { return state.PC; }
	__forceinline uint64_t GetCycleCount(GenesisM68KState& state) { return 0; }
	__forceinline uint32_t GetStackPointer(GenesisM68KState& state) { return state.A[7]; }
};

class GenesisZ80TraceLogger : public BaseTraceLogger<GenesisZ80TraceLogger, GenesisZ80State>
{
private:
	GenesisVdp* _vdp = nullptr;

protected:
	RowDataType GetFormatTagType(string& tag) override;

public:
	GenesisZ80TraceLogger(Debugger* debugger, IDebugger* cpuDebugger, GenesisVdp* vdp);

	void GetTraceRow(string& output, GenesisZ80State& cpuState, TraceLogPpuState& vdpState, DisassemblyInfo& disassemblyInfo);
	void LogPpuState();

	__forceinline uint32_t GetProgramCounter(GenesisZ80State& state) { return state.PC; }
	__forceinline uint64_t GetCycleCount(GenesisZ80State& state) { return 0; }
	__forceinline uint16_t GetStackPointer(GenesisZ80State& state) { return state.SP; }
};
