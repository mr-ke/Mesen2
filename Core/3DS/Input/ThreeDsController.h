#pragma once
#include "pch.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/InputHud.h"
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

	void InternalDrawController(InputHud& hud) override
	{
		hud.DrawOutline(50, 20);

		// D-pad
		hud.DrawButton(5, 5, 3, 3, IsPressed(Buttons::Up));
		hud.DrawButton(5, 11, 3, 3, IsPressed(Buttons::Down));
		hud.DrawButton(2, 8, 3, 3, IsPressed(Buttons::Left));
		hud.DrawButton(8, 8, 3, 3, IsPressed(Buttons::Right));
		hud.DrawButton(5, 8, 3, 3, false);

		// Circle Pad (left analog)
		hud.DrawButton(15, 8, 4, 4, IsPressed(Buttons::CirclePadUp) || IsPressed(Buttons::CirclePadDown) || 
		                          IsPressed(Buttons::CirclePadLeft) || IsPressed(Buttons::CirclePadRight));

		// ABXY buttons
		hud.DrawButton(42, 6, 3, 3, IsPressed(Buttons::X));
		hud.DrawButton(42, 12, 3, 3, IsPressed(Buttons::B));
		hud.DrawButton(39, 9, 3, 3, IsPressed(Buttons::Y));
		hud.DrawButton(45, 9, 3, 3, IsPressed(Buttons::A));

		// C-Stick (right analog)
		hud.DrawButton(35, 5, 3, 3, IsPressed(Buttons::CStickUp) || IsPressed(Buttons::CStickDown) || 
		                          IsPressed(Buttons::CStickLeft) || IsPressed(Buttons::CStickRight));

		// L, ZL, R, ZR buttons
		hud.DrawButton(4, 0, 5, 2, IsPressed(Buttons::L));
		hud.DrawButton(10, 0, 4, 2, IsPressed(Buttons::ZL));
		hud.DrawButton(36, 0, 5, 2, IsPressed(Buttons::R));
		hud.DrawButton(31, 0, 4, 2, IsPressed(Buttons::ZR));

		// Start and Select
		hud.DrawButton(21, 13, 4, 2, IsPressed(Buttons::Select));
		hud.DrawButton(26, 13, 4, 2, IsPressed(Buttons::Start));

		// Home button
		hud.DrawButton(23, 8, 4, 4, IsPressed(Buttons::Home));

		hud.DrawNumber(_port + 1, 23, 4);
	}

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
