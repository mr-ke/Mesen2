#include "pch.h"
#include "Genesis/Debugger/GenesisTraceLogger.h"
#include "Genesis/GenesisVdp.h"
#include "Genesis/GenesisTypes.h"
#include "Debugger/DisassemblyInfo.h"
#include "Debugger/Debugger.h"
#include "Debugger/DebugTypes.h"
#include "Utilities/HexUtilities.h"

GenesisM68KTraceLogger::GenesisM68KTraceLogger(Debugger* debugger, IDebugger* cpuDebugger, GenesisVdp* vdp) : BaseTraceLogger(debugger, cpuDebugger, CpuType::GenesisM68K)
{
	_vdp = vdp;
}

RowDataType GenesisM68KTraceLogger::GetFormatTagType(string& tag)
{
	if(tag == "PC") {
		return RowDataType::PC;
	} else if(tag == "SP") {
		return RowDataType::SP;
	} else if(tag == "SR") {
		return RowDataType::SR;
	} else if(tag.size() == 2 && tag[0] == 'D' && tag[1] >= '0' && tag[1] <= '7') {
		return (RowDataType)((int)RowDataType::R0 + (tag[1] - '0'));
	} else if(tag.size() == 2 && tag[0] == 'A' && tag[1] >= '0' && tag[1] <= '7') {
		return (RowDataType)((int)RowDataType::R8 + (tag[1] - '0'));
	} else {
		return RowDataType::Text;
	}
}

void GenesisM68KTraceLogger::GetTraceRow(string& output, GenesisM68KState& cpuState, TraceLogPpuState& vdpState, DisassemblyInfo& disassemblyInfo)
{
	for(RowPart& rowPart : _rowParts) {
		switch(rowPart.DataType) {
			case RowDataType::PC: WriteIntValue(output, cpuState.PC, rowPart); break;
			case RowDataType::SP: WriteIntValue(output, cpuState.A[7], rowPart); break;
			case RowDataType::SR: WriteIntValue(output, cpuState.SR, rowPart); break;
			case RowDataType::R0: WriteIntValue(output, cpuState.D[0], rowPart); break;
			case RowDataType::R1: WriteIntValue(output, cpuState.D[1], rowPart); break;
			case RowDataType::R2: WriteIntValue(output, cpuState.D[2], rowPart); break;
			case RowDataType::R3: WriteIntValue(output, cpuState.D[3], rowPart); break;
			case RowDataType::R4: WriteIntValue(output, cpuState.D[4], rowPart); break;
			case RowDataType::R5: WriteIntValue(output, cpuState.D[5], rowPart); break;
			case RowDataType::R6: WriteIntValue(output, cpuState.D[6], rowPart); break;
			case RowDataType::R7: WriteIntValue(output, cpuState.D[7], rowPart); break;
			case RowDataType::R8: WriteIntValue(output, cpuState.A[0], rowPart); break;
			case RowDataType::R9: WriteIntValue(output, cpuState.A[1], rowPart); break;
			case RowDataType::R10: WriteIntValue(output, cpuState.A[2], rowPart); break;
			case RowDataType::R11: WriteIntValue(output, cpuState.A[3], rowPart); break;
			case RowDataType::R12: WriteIntValue(output, cpuState.A[4], rowPart); break;
			case RowDataType::R13: WriteIntValue(output, cpuState.A[5], rowPart); break;
			case RowDataType::R14: WriteIntValue(output, cpuState.A[6], rowPart); break;
			case RowDataType::R15: WriteIntValue(output, cpuState.A[7], rowPart); break;
			default: ProcessSharedTag(rowPart, output, cpuState, vdpState, disassemblyInfo); break;
		}
	}
}

void GenesisM68KTraceLogger::LogPpuState()
{
	_ppuState[_currentPos] = {
		_vdp->GetHCounter(),
		_vdp->GetHCounter(),
		_vdp->GetVCounter(),
		_vdp->GetFrameCount()
	};
}

GenesisZ80TraceLogger::GenesisZ80TraceLogger(Debugger* debugger, IDebugger* cpuDebugger, GenesisVdp* vdp) : BaseTraceLogger(debugger, cpuDebugger, CpuType::GenesisZ80)
{
	_vdp = vdp;
}

RowDataType GenesisZ80TraceLogger::GetFormatTagType(string& tag)
{
	if(tag == "A") {
		return RowDataType::A;
	} else if(tag == "B") {
		return RowDataType::B;
	} else if(tag == "C") {
		return RowDataType::C;
	} else if(tag == "D") {
		return RowDataType::D;
	} else if(tag == "E") {
		return RowDataType::E;
	} else if(tag == "F") {
		return RowDataType::F;
	} else if(tag == "H") {
		return RowDataType::H;
	} else if(tag == "L") {
		return RowDataType::L;
	} else if(tag == "I") {
		return RowDataType::I;
	} else if(tag == "R") {
		return RowDataType::R;
	} else if(tag == "IX") {
		return RowDataType::IX;
	} else if(tag == "IY") {
		return RowDataType::IY;
	} else if(tag == "PC") {
		return RowDataType::PC;
	} else if(tag == "SP") {
		return RowDataType::SP;
	} else if(tag == "PS") {
		return RowDataType::PS;
	} else {
		return RowDataType::Text;
	}
}

void GenesisZ80TraceLogger::GetTraceRow(string& output, GenesisZ80State& cpuState, TraceLogPpuState& vdpState, DisassemblyInfo& disassemblyInfo)
{
	constexpr char activeStatusLetters[8] = { 'S', 'Z', '5', 'H', '3', 'P', 'N', 'C' };
	constexpr char inactiveStatusLetters[8] = { 's', 'z', '-', 'h', '-', 'p', 'n', 'c' };

	for(RowPart& rowPart : _rowParts) {
		switch(rowPart.DataType) {
			case RowDataType::A: WriteIntValue(output, cpuState.A, rowPart); break;
			case RowDataType::B: WriteIntValue(output, cpuState.B, rowPart); break;
			case RowDataType::C: WriteIntValue(output, cpuState.C, rowPart); break;
			case RowDataType::D: WriteIntValue(output, cpuState.D, rowPart); break;
			case RowDataType::E: WriteIntValue(output, cpuState.E, rowPart); break;
			case RowDataType::F: WriteIntValue(output, cpuState.Flags, rowPart); break;
			case RowDataType::H: WriteIntValue(output, cpuState.H, rowPart); break;
			case RowDataType::L: WriteIntValue(output, cpuState.L, rowPart); break;
			case RowDataType::I: WriteIntValue(output, cpuState.I, rowPart); break;
			case RowDataType::R: WriteIntValue(output, cpuState.R, rowPart); break;
			case RowDataType::IX: WriteIntValue(output, cpuState.IX, rowPart); break;
			case RowDataType::IY: WriteIntValue(output, cpuState.IY, rowPart); break;
			case RowDataType::SP: WriteIntValue(output, cpuState.SP, rowPart); break;
			case RowDataType::PC: WriteIntValue(output, cpuState.PC, rowPart); break;
			case RowDataType::PS: GetStatusFlag(activeStatusLetters, inactiveStatusLetters, output, cpuState.Flags, rowPart, 8); break;
			default: ProcessSharedTag(rowPart, output, cpuState, vdpState, disassemblyInfo); break;
		}
	}
}

void GenesisZ80TraceLogger::LogPpuState()
{
	_ppuState[_currentPos] = {
		_vdp->GetHCounter(),
		_vdp->GetHCounter(),
		_vdp->GetVCounter(),
		_vdp->GetFrameCount()
	};
}
