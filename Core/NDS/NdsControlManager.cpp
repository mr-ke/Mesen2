#include "pch.h"
#include "NdsControlManager.h"
#include "NdsConsole.h"
#include "Input/NdsController.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/KeyManager.h"
#include "Core/Shared/CpuType.h"

NdsControlManager::NdsControlManager(Emulator* emu, NdsConsole* console)
	: BaseControlManager(emu, CpuType::Nds)
{
	_console = console;
}

void NdsControlManager::UpdateInputState()
{
	BaseControlManager::UpdateInputState();
}

shared_ptr<BaseControlDevice> NdsControlManager::CreateControllerDevice(ControllerType type, uint8_t port)
{
	shared_ptr<BaseControlDevice> device;

	NdsConfig& cfg = _emu->GetSettings()->GetNdsConfig();

	switch(type) {
		default:
		case ControllerType::None: break;

		case ControllerType::NdsController: device.reset(new NdsController(_emu, port, cfg.Controller.Keys)); break;
	}

	return device;
}

void NdsControlManager::UpdateControlDevices()
{
	NdsConfig cfg = _emu->GetSettings()->GetNdsConfig();
	if(_emu->GetSettings()->IsEqual(_prevConfig, cfg) && _controlDevices.size() > 0) {
		//Do nothing if configuration is unchanged
		return;
	}

	auto lock = _deviceLock.AcquireSafe();

	ClearDevices();

	shared_ptr<BaseControlDevice> device(CreateControllerDevice(ControllerType::NdsController, 0));
	if(device) {
		RegisterControlDevice(device);
	}
}

void NdsControlManager::Serialize(Serializer& s)
{
	BaseControlManager::Serialize(s);
}
