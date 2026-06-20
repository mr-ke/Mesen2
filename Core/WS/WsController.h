#pragma once
#include "pch.h"
#include "WS/WsConsole.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Utilities/Serializer.h"

class WsController : public BaseControlDevice
{
private:
	WsConsole* _console = nullptr;
	vector<KeyMapping> _verticalMappings;
	uint32_t _turboSpeed = 0;

protected:
	string GetKeyNames() override
	{
		return "UDLRudlrSsBA";
	}

	void InternalSetStateFromInput() override
	{
		vector<KeyMapping>& keyMappings = _console->IsVerticalMode() ? _verticalMappings : _keyMappings;
		for(KeyMapping& keyMapping : keyMappings) {
			SetPressedState(Buttons::A, keyMapping.A);
			SetPressedState(Buttons::B, keyMapping.B);
			SetPressedState(Buttons::Sound, keyMapping.GenericKey1);
			SetPressedState(Buttons::Start, keyMapping.Start);
			SetPressedState(Buttons::Up, keyMapping.Up);
			SetPressedState(Buttons::Down, keyMapping.Down);
			SetPressedState(Buttons::Left, keyMapping.Left);
			SetPressedState(Buttons::Right, keyMapping.Right);

			SetPressedState(Buttons::Up2, keyMapping.U);
			SetPressedState(Buttons::Down2, keyMapping.D);
			SetPressedState(Buttons::Left2, keyMapping.L);
			SetPressedState(Buttons::Right2, keyMapping.R);

			uint8_t turboFreq = 1 << (4 - _turboSpeed);
			bool turboOn = (uint8_t)(_emu->GetFrameCount() % turboFreq) < turboFreq / 2;
			if(turboOn) {
				SetPressedState(Buttons::A, keyMapping.TurboA);
				SetPressedState(Buttons::B, keyMapping.TurboB);
			}
		}
	}

	void RefreshStateBuffer() override
	{}

public:
	enum Buttons { Up = 0, Down, Left, Right, Up2, Down2, Left2, Right2, Sound, Start, B, A };

	WsController(Emulator* emu, WsConsole* console, uint8_t port, KeyMappingSet horizontalMappings, KeyMappingSet verticalMappings) : BaseControlDevice(emu, ControllerType::WsController, port, horizontalMappings)
	{
		//TODOWS turbo support
		_verticalMappings = verticalMappings.GetKeyMappingArray();
		_console = console;
		_turboSpeed = horizontalMappings.TurboSpeed;
	}

	uint8_t ReadRam(uint16_t addr) override
	{
		return 0;
	}

	void WriteRam(uint16_t addr, uint8_t value) override
	{
	}

	vector<DeviceButtonName> GetKeyNameAssociations() override
	{
		return {
			{ "a", Buttons::A },
			{ "b", Buttons::B },
			{ "sound", Buttons::Sound },
			{ "start", Buttons::Start },
			{ "up", Buttons::Up },
			{ "down", Buttons::Down },
			{ "left", Buttons::Left },
			{ "right", Buttons::Right },
			{ "up2", Buttons::Up2 },
			{ "down2", Buttons::Down2 },
			{ "left2", Buttons::Left2 },
			{ "right2", Buttons::Right2 },
		};
	}
};