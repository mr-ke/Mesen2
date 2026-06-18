#pragma once
#include "Shared/BaseControlManager.h"
#include "Shared/SettingTypes.h"

class Emulator;
class ThreeDsConsole;
class BaseControlDevice;

class ThreeDsControlManager final : public BaseControlManager
{
private:
	ThreeDsConsole* _console = nullptr;
	ThreeDsConfig _prevConfig = {};

public:
	ThreeDsControlManager(Emulator* emu, ThreeDsConsole* console);
	
	void UpdateInputState() override;
	shared_ptr<BaseControlDevice> CreateControllerDevice(ControllerType type, uint8_t port) override;
	void UpdateControlDevices() override;
	
	void Serialize(Serializer& s) override;
};
