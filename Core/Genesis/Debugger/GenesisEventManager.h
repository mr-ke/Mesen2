#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"
#include "Debugger/BaseEventManager.h"
#include "Utilities/SimpleLock.h"

enum class DebugEventType;
struct DebugEventInfo;
class GenesisConsole;
class GenesisVdp;
class Debugger;

struct GenesisEventViewerConfig : public BaseEventViewerConfig
{
	EventViewerCategoryCfg Irq;
	EventViewerCategoryCfg Nmi;
	EventViewerCategoryCfg MarkedBreakpoints;

	EventViewerCategoryCfg VdpPaletteWrite;
	EventViewerCategoryCfg VdpVramWrite;
	EventViewerCategoryCfg VdpVramRead;
	EventViewerCategoryCfg VdpControlPortWrite;
	EventViewerCategoryCfg VdpControlPortRead;

	EventViewerCategoryCfg IoWrite;
	EventViewerCategoryCfg IoRead;
	EventViewerCategoryCfg PsgWrite;
	EventViewerCategoryCfg Ym2612Write;

	EventViewerCategoryCfg Z80BusRequest;
	EventViewerCategoryCfg Z80Reset;

	bool ShowPreviousFrameEvents;
};

class GenesisEventManager final : public BaseEventManager
{
private:
	static constexpr int ScanlineWidth = 488;
	static constexpr int ScreenHeight = 313;

	GenesisEventViewerConfig _config;

	GenesisConsole* _console;
	GenesisVdp* _vdp;
	Debugger* _debugger;

	uint32_t _scanlineCount = 262;
	uint32_t _visibleScanlineCount = 224;
	uint32_t* _ppuBuffer = nullptr;

protected:
	bool ShowPreviousFrameEvents() override;
	void ConvertScanlineCycleToRowColumn(int32_t& x, int32_t& y) override;
	void DrawScreen(uint32_t* buffer) override;

public:
	GenesisEventManager(Debugger* debugger, GenesisConsole* console, GenesisVdp* vdp, CpuType cpuType);
	~GenesisEventManager();

	void AddEvent(DebugEventType type, MemoryOperationInfo& operation, int32_t breakpointId = -1) override;
	void AddEvent(DebugEventType type) override;

	EventViewerCategoryCfg GetEventConfig(DebugEventInfo& evt) override;

	uint32_t TakeEventSnapshot(bool forAutoRefresh) override;
	DebugEventInfo GetEvent(uint16_t y, uint16_t x) override;

	FrameInfo GetDisplayBufferSize() override;
	void SetConfiguration(BaseEventViewerConfig& config) override;
};
