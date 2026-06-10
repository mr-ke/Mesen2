#pragma once
#include "Shared/BaseControlManager.h"
#include "Shared/SettingTypes.h"

class Emulator;
class NdsConsole;
class BaseControlDevice;

class NdsControlManager final : public BaseControlManager
{
private:
	NdsConsole* _console = nullptr;
	NdsConfig _prevConfig = {};

public:
	NdsControlManager(Emulator* emu, NdsConsole* console);
	
	void UpdateInputState() override;
	shared_ptr<BaseControlDevice> CreateControllerDevice(ControllerType type, uint8_t port) override;
	void UpdateControlDevices() override;
	
	void Serialize(Serializer& s) override;
};
