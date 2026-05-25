#include "pch.h"
#include "MinGWKeyManager.h"
#include "Core/Shared/Emulator.h"
#include "Core/Shared/MessageManager.h"
#include <windows.h>

MinGWKeyManager::MinGWKeyManager(Emulator* emu, void* windowHandle)
{
	_emu = emu;
	_windowHandle = windowHandle;
	_disabled = false;
	memset(_keyState, 0, sizeof(_keyState));
	
	_keyDefinitions = KeyDefinition::GetSharedKeyDefinitions();
	
	_keyDefinitions.push_back({ "Left Mouse Button", IKeyManager::BaseMouseButtonIndex + 0 });
	_keyDefinitions.push_back({ "Right Mouse Button", IKeyManager::BaseMouseButtonIndex + 1 });
	_keyDefinitions.push_back({ "Middle Mouse Button", IKeyManager::BaseMouseButtonIndex + 2 });
	_keyDefinitions.push_back({ "Mouse Button 4", IKeyManager::BaseMouseButtonIndex + 3 });
	_keyDefinitions.push_back({ "Mouse Button 5", IKeyManager::BaseMouseButtonIndex + 4 });
	
	for(KeyDefinition& keyDef : _keyDefinitions) {
		_keyNames[keyDef.keyCode] = keyDef.name;
		_keyCodes[keyDef.name] = keyDef.keyCode;
	}
}

MinGWKeyManager::~MinGWKeyManager()
{
}

void MinGWKeyManager::RefreshState()
{
}

bool MinGWKeyManager::IsKeyPressed(uint16_t key)
{
	if(_disabled) {
		return false;
	}
	if(key == 0) return false;
	
	if(key < 0x205) {
		return _keyState[key] != 0;
	}
	return false;
}

optional<int16_t> MinGWKeyManager::GetAxisPosition(uint16_t key)
{
	return std::nullopt;
}

bool MinGWKeyManager::IsMouseButtonPressed(MouseButton button)
{
	if(_disabled) return false;
	
	uint16_t keyCode = IKeyManager::BaseMouseButtonIndex + (uint16_t)button;
	if(keyCode < 0x205) {
		return _keyState[keyCode] != 0;
	}
	return false;
}

std::vector<uint16_t> MinGWKeyManager::GetPressedKeys()
{
	std::vector<uint16_t> pressedKeys;
	
	for(int i = 0; i < 0x205; i++) {
		if(_keyState[i]) {
			pressedKeys.push_back(i);
		}
	}
	
	return pressedKeys;
}

string MinGWKeyManager::GetKeyName(uint16_t key)
{
	auto it = _keyNames.find(key);
	if(it != _keyNames.end()) {
		return it->second;
	}
	return "";
}

uint16_t MinGWKeyManager::GetKeyCode(string keyName)
{
	auto it = _keyCodes.find(keyName);
	if(it != _keyCodes.end()) {
		return it->second;
	}
	return 0;
}

void MinGWKeyManager::UpdateDevices()
{
}

bool MinGWKeyManager::SetKeyState(uint16_t scanCode, bool state)
{
	if(scanCode < 0x205) {
		_keyState[scanCode] = state;
		return true;
	}
	return false;
}

void MinGWKeyManager::ResetKeyState()
{
	memset(_keyState, 0, sizeof(_keyState));
}

void MinGWKeyManager::SetDisabled(bool disabled)
{
	_disabled = disabled;
}

void MinGWKeyManager::SetForceFeedback(uint16_t magnitudeRight, uint16_t magnitudeLeft)
{
}
