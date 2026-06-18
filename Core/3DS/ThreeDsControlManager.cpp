#include "pch.h"
#include "ThreeDsControlManager.h"
#include "ThreeDsConsole.h"
#include "Input/ThreeDsController.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/EmuSettings.h"
#include "Core/Shared/KeyManager.h"
#include "Core/Shared/InputHud.h"
#include "Core/Shared/CpuType.h"

ThreeDsControlManager::ThreeDsControlManager(Emulator* emu, ThreeDsConsole* console)
	: BaseControlManager(emu, CpuType::ThreeDs)
{
	_console = console;
}

void ThreeDsControlManager::UpdateInputState()
{
	BaseControlManager::UpdateInputState();
}

shared_ptr<BaseControlDevice> ThreeDsControlManager::CreateControllerDevice(ControllerType type, uint8_t port)
{
	shared_ptr<BaseControlDevice> device;

	ThreeDsConfig& cfg = _emu->GetSettings()->GetThreeDsConfig();

	switch(type) {
		default:
		case ControllerType::None: break;

		case ControllerType::ThreeDsController: device.reset(new ThreeDsController(_emu, port, cfg.Controller.Keys)); break;
	}

	return device;
}

void ThreeDsControlManager::UpdateControlDevices()
{
	ThreeDsConfig cfg = _emu->GetSettings()->GetThreeDsConfig();
	if(_emu->GetSettings()->IsEqual(_prevConfig, cfg) && _controlDevices.size() > 0) {
		//Do nothing if configuration is unchanged
		return;
	}

	auto lock = _deviceLock.AcquireSafe();

	ClearDevices();

	shared_ptr<BaseControlDevice> device(CreateControllerDevice(ControllerType::ThreeDsController, 0));
	if(device) {
		RegisterControlDevice(device);
	}
}

void ThreeDsControlManager::Serialize(Serializer& s)
{
	BaseControlManager::Serialize(s);
}
