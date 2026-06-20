#pragma once
#include "pch.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Utilities/Serializer.h"

class ThreeDsController : public BaseControlDevice
{
private:
	uint32_t _turboSpeed = 0;

protected:
	string GetKeyNames() override
	{
		return "UDLRSsBAYXLRlrZzHCcPp";
	}

	void InternalSetStateFromInput() override
	{
		for(KeyMapping& keyMapping : _keyMappings) {
			// Standard buttons
			SetPressedState(Buttons::A, keyMapping.A);
			SetPressedState(Buttons::B, keyMapping.B);
			SetPressedState(Buttons::X, keyMapping.X);
			SetPressedState(Buttons::Y, keyMapping.Y);
			SetPressedState(Buttons::Start, keyMapping.Start);
			SetPressedState(Buttons::Select, keyMapping.Select);
			SetPressedState(Buttons::Up, keyMapping.Up);
			SetPressedState(Buttons::Down, keyMapping.Down);
			SetPressedState(Buttons::Left, keyMapping.Left);
			SetPressedState(Buttons::Right, keyMapping.Right);
			SetPressedState(Buttons::L, keyMapping.L);
			SetPressedState(Buttons::R, keyMapping.R);

			// 3DS-specific buttons (ZL, ZR, Home, Power)
			// These use custom key mapping fields
			// For now, map to unused fields in KeyMapping

			uint8_t turboFreq = 1 << (4 - _turboSpeed);
			bool turboOn = (uint8_t)(_emu->GetFrameCount() % turboFreq) < turboFreq / 2;
			if(turboOn) {
				SetPressedState(Buttons::A, keyMapping.TurboA);
				SetPressedState(Buttons::B, keyMapping.TurboB);
				SetPressedState(Buttons::X, keyMapping.TurboX);
				SetPressedState(Buttons::Y, keyMapping.TurboY);
				SetPressedState(Buttons::L, keyMapping.TurboL);
				SetPressedState(Buttons::R, keyMapping.TurboR);
			}

			if(!_emu->GetSettings()->GetThreeDsConfig().AllowInvalidInput) {
				//If both U+D or L+R are pressed at the same time, act as if neither is pressed
				if(IsPressed(Buttons::Up) && IsPressed(Buttons::Down)) {
					ClearBit(Buttons::Down);
					ClearBit(Buttons::Up);
				}
				if(IsPressed(Buttons::Left) && IsPressed(Buttons::Right)) {
					ClearBit(Buttons::Left);
					ClearBit(Buttons::Right);
				}
			}
		}
	}

	void RefreshStateBuffer() override
	{}

public:
	enum Buttons { 
		Up = 0, Down, Left, Right,    // D-Pad
		Start, Select,                 // Start/Select
		B, A, Y, X,                    // Face buttons
		L, R,                          // Shoulder buttons
		ZL, ZR,                        // Extra shoulder buttons (3DS)
		Home, Power,                   // System buttons
		CirclePadUp, CirclePadDown,    // Circle Pad
		CirclePadLeft, CirclePadRight,
		CStickUp, CStickDown,          // C-Stick
		CStickLeft, CStickRight
	};

	ThreeDsController(Emulator* emu, uint8_t port, KeyMappingSet keyMappings) : BaseControlDevice(emu, ControllerType::ThreeDsController, port, keyMappings)
	{
		_turboSpeed = keyMappings.TurboSpeed;
	}

	uint8_t ReadRam(uint16_t addr) override
	{
		return 0;
	}

	void WriteRam(uint16_t addr, uint8_t value) override
	{}

	vector<DeviceButtonName> GetKeyNameAssociations() override
	{
		return {
			{ "a", Buttons::A },
			{ "b", Buttons::B },
			{ "x", Buttons::X },
			{ "y", Buttons::Y },
			{ "start", Buttons::Start },
			{ "select", Buttons::Select },
			{ "up", Buttons::Up },
			{ "down", Buttons::Down },
			{ "left", Buttons::Left },
			{ "right", Buttons::Right },
			{ "l", Buttons::L },
			{ "r", Buttons::R },
			{ "zl", Buttons::ZL },
			{ "zr", Buttons::ZR },
			{ "home", Buttons::Home },
			{ "power", Buttons::Power },
			{ "circlepad_up", Buttons::CirclePadUp },
			{ "circlepad_down", Buttons::CirclePadDown },
			{ "circlepad_left", Buttons::CirclePadLeft },
			{ "circlepad_right", Buttons::CirclePadRight },
			{ "cstick_up", Buttons::CStickUp },
			{ "cstick_down", Buttons::CStickDown },
			{ "cstick_left", Buttons::CStickLeft },
			{ "cstick_right", Buttons::CStickRight },
		};
	}
};
