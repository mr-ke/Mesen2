#include "pch.h"
#include "Genesis/Debugger/GenesisEventManager.h"
#include "Genesis/GenesisVdp.h"
#include "Genesis/GenesisConsole.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/Debugger.h"
#include "Debugger/DebugBreakHelper.h"
#include "Debugger/BaseEventManager.h"
#include "Shared/SettingTypes.h"

GenesisEventManager::GenesisEventManager(Debugger* debugger, GenesisConsole* console, GenesisVdp* vdp, CpuType cpuType)
{
	_debugger = debugger;
	_console = console;
	_vdp = vdp;

	if(console->GetRegion() == ConsoleRegion::Pal) {
		_scanlineCount = 313;
		_visibleScanlineCount = 240;
	} else {
		_scanlineCount = 262;
		_visibleScanlineCount = 224;
	}

	_ppuBuffer = new uint32_t[ScanlineWidth * ScreenHeight];
	memset(_ppuBuffer, 0, ScanlineWidth * ScreenHeight * sizeof(uint32_t));
}

GenesisEventManager::~GenesisEventManager()
{
	delete[] _ppuBuffer;
}

void GenesisEventManager::AddEvent(DebugEventType type, MemoryOperationInfo& operation, int32_t breakpointId)
{
	DebugEventInfo evt = {};
	evt.Type = type;
	evt.Flags = (uint32_t)EventFlags::ReadWriteOp;
	evt.Operation = operation;
	evt.Scanline = _vdp->GetVCounter();
	evt.Cycle = _vdp->GetHCounter();
	evt.BreakpointId = breakpointId;
	evt.DmaChannel = -1;

	if(evt.Operation.Type == MemoryOperationType::Write) {
		uint32_t addr = evt.Operation.Address & 0x1F;
		switch(addr) {
			case 0x00: case 0x01: //Data port
			case 0x02: case 0x03: //Data port (alternate)
			case 0x04: case 0x05: //Control port
			case 0x06: case 0x07: //Control port (alternate)
				evt.Flags |= (uint32_t)EventFlags::WithTargetMemory;
				evt.TargetMemory.MemType = MemoryType::GenesisVdpVram;
				evt.TargetMemory.Value = operation.Value;
				evt.TargetMemory.Type = operation.Type;
				evt.TargetMemory.Address = operation.Address;
				break;

			default:
				if(addr >= 0x10 && addr <= 0x17) {
					//PSG
					evt.Flags |= (uint32_t)EventFlags::WithTargetMemory;
					evt.TargetMemory.MemType = MemoryType::None;
					evt.TargetMemory.Value = operation.Value;
					evt.TargetMemory.Type = operation.Type;
				}
				break;
		}
	}

	evt.ProgramCounter = _debugger->GetProgramCounter(CpuType::GenesisM68K, true);
	_debugEvents.push_back(evt);
}

void GenesisEventManager::AddEvent(DebugEventType type)
{
	DebugEventInfo evt = {};
	evt.Type = type;
	evt.Scanline = _vdp->GetVCounter();
	evt.Cycle = _vdp->GetHCounter();
	evt.BreakpointId = -1;
	evt.DmaChannel = -1;
	evt.ProgramCounter = _debugger->GetProgramCounter(CpuType::GenesisM68K, true);
	_debugEvents.push_back(evt);
}

DebugEventInfo GenesisEventManager::GetEvent(uint16_t y, uint16_t x)
{
	auto lock = _lock.AcquireSafe();

	//Search without including larger background color first
	for(DebugEventInfo& evt : _sentEvents) {
		if(evt.Cycle == x && evt.Scanline == y) {
			return evt;
		}
	}

	//If no exact match, extend to the background color
	for(int i = (int)_sentEvents.size() - 1; i >= 0; i--){
		DebugEventInfo& evt = _sentEvents[i];
		if(std::abs((int)evt.Cycle - (int)x) <= 1 && std::abs((int)evt.Scanline - (int)y) <= 1) {
			return evt;
		}
	}

	DebugEventInfo empty = {};
	empty.ProgramCounter = 0xFFFFFFFF;
	return empty;
}

bool GenesisEventManager::ShowPreviousFrameEvents()
{
	return _config.ShowPreviousFrameEvents;
}

void GenesisEventManager::SetConfiguration(BaseEventViewerConfig& config)
{
	_config = (GenesisEventViewerConfig&)config;
}

EventViewerCategoryCfg GenesisEventManager::GetEventConfig(DebugEventInfo& evt)
{
	switch(evt.Type) {
		default: return {};
		case DebugEventType::Breakpoint: return _config.MarkedBreakpoints;
		case DebugEventType::Irq: return _config.Irq;
		case DebugEventType::Nmi: return _config.Nmi;
		case DebugEventType::Register:
			if(evt.Operation.Type == MemoryOperationType::Read) {
				switch(evt.Operation.Address & 0x1F) {
					case 0x00: case 0x01: case 0x02: case 0x03: return _config.VdpVramRead;
					case 0x04: case 0x05: case 0x06: case 0x07: return _config.VdpControlPortRead;
					default: return {};
				}
			} else {
				switch(evt.Operation.Address & 0x1F) {
					case 0x00: case 0x01: case 0x02: case 0x03: return _config.VdpVramWrite;
					case 0x04: case 0x05: case 0x06: case 0x07: return _config.VdpControlPortWrite;
					case 0x10: case 0x11: case 0x12: case 0x13:
					case 0x14: case 0x15: case 0x16: case 0x17: return _config.PsgWrite;
					case 0x18: case 0x19: case 0x1A: case 0x1B:
					case 0x1C: case 0x1D: case 0x1E: case 0x1F: return _config.Ym2612Write;
					default: return {};
				}
			}
	}
}

void GenesisEventManager::ConvertScanlineCycleToRowColumn(int32_t& x, int32_t& y)
{
	//Genesis uses a 1:1 mapping of scanline/cycle to row/column
	//x = cycle, y = scanline
}

uint32_t GenesisEventManager::TakeEventSnapshot(bool forAutoRefresh)
{
	DebugBreakHelper breakHelper(_debugger);
	auto lock = _lock.AcquireSafe();

	uint16_t cycle = _vdp->GetHCounter();
	uint16_t scanline = _vdp->GetVCounter();

	uint32_t screenWidth = _vdp->GetScreenWidth();
	uint32_t screenHeight = _vdp->GetScreenHeight();

	if(scanline >= _visibleScanlineCount || (forAutoRefresh && (scanline == 0 && cycle == 0))) {
		memcpy(_ppuBuffer, _vdp->GetFramebuffer(), screenWidth * screenHeight * sizeof(uint32_t));
	} else {
		uint32_t offset = screenWidth * scanline;
		memcpy(_ppuBuffer, _vdp->GetFramebuffer(), offset * sizeof(uint32_t));
		memcpy(_ppuBuffer + offset, _vdp->GetFramebuffer() + offset, (screenWidth * screenHeight - offset) * sizeof(uint32_t));
	}

	_snapshotCurrentFrame = _debugEvents;
	_snapshotPrevFrame = _prevDebugEvents;
	_snapshotScanline = scanline;
	_snapshotCycle = cycle;
	_forAutoRefresh = forAutoRefresh;
	return _scanlineCount;
}

FrameInfo GenesisEventManager::GetDisplayBufferSize()
{
	FrameInfo size;
	size.Width = ScanlineWidth;
	size.Height = ScreenHeight;
	return size;
}

void GenesisEventManager::DrawScreen(uint32_t* buffer)
{
	uint32_t screenWidth = _vdp->GetScreenWidth();
	uint32_t screenHeight = _vdp->GetScreenHeight();

	uint32_t* src = _ppuBuffer;
	for(uint32_t y = 0; y < screenHeight && y < ScreenHeight; y++) {
		for(uint32_t x = 0; x < screenWidth && x < ScanlineWidth; x++) {
			buffer[y * ScanlineWidth + x] = src[y * screenWidth + x];
		}
	}
}
