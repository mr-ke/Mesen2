#pragma once
#include "Shared/Interfaces/IKeyManager.h"
#include "Shared/KeyDefinitions.h"
#include <unordered_map>
#include <vector>

class Emulator;

class MinGWKeyManager : public IKeyManager
{
private:
	Emulator* _emu;
	void* _windowHandle;
	vector<KeyDefinition> _keyDefinitions;
	bool _keyState[0x205];
	std::unordered_map<uint16_t, string> _keyNames;
	std::unordered_map<string, uint16_t> _keyCodes;
	bool _disabled;

public:
	MinGWKeyManager(Emulator* emu, void* windowHandle);
	virtual ~MinGWKeyManager();

	void RefreshState() override;
	bool IsKeyPressed(uint16_t key) override;
	optional<int16_t> GetAxisPosition(uint16_t key) override;
	bool IsMouseButtonPressed(MouseButton button) override;
	std::vector<uint16_t> GetPressedKeys() override;
	string GetKeyName(uint16_t key) override;
	uint16_t GetKeyCode(string keyName) override;

	void UpdateDevices() override;
	bool SetKeyState(uint16_t scanCode, bool state) override;
	void ResetKeyState() override;

	void SetDisabled(bool disabled) override;

	void SetForceFeedback(uint16_t magnitudeRight, uint16_t magnitudeLeft) override;
};
