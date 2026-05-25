#include "pch.h"
#include "MinGWMouseManager.h"
#include <windows.h>

MinGWMouseManager::MinGWMouseManager(void* windowHandle)
{
	_windowHandle = windowHandle;
}

MinGWMouseManager::~MinGWMouseManager()
{
}

SystemMouseState MinGWMouseManager::GetSystemMouseState(void* rendererHandle)
{
	SystemMouseState state = {};

	if(_windowHandle) {
		HWND hwnd = (HWND)_windowHandle;
		POINT point;
		GetCursorPos(&point);
		ScreenToClient(hwnd, &point);

		state.XPosition = point.x;
		state.YPosition = point.y;

		state.LeftButton = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
		state.RightButton = (GetKeyState(VK_RBUTTON) & 0x8000) != 0;
		state.MiddleButton = (GetKeyState(VK_MBUTTON) & 0x8000) != 0;
		state.Button4 = (GetKeyState(VK_XBUTTON1) & 0x8000) != 0;
		state.Button5 = (GetKeyState(VK_XBUTTON2) & 0x8000) != 0;
	}

	return state;
}

bool MinGWMouseManager::CaptureMouse(int32_t x, int32_t y, int32_t width, int32_t height, void* rendererHandle)
{
	if(_windowHandle) {
		HWND hwnd = (HWND)_windowHandle;
		RECT rect;
		rect.left = x;
		rect.top = y;
		rect.right = x + width;
		rect.bottom = y + height;
		
		POINT point;
		point.x = x + width / 2;
		point.y = y + height / 2;
		ClientToScreen(hwnd, &point);
		SetCursorPos(point.x, point.y);
		
		ShowCursor(FALSE);
		return true;
	}
	return false;
}

void MinGWMouseManager::ReleaseMouse()
{
	ShowCursor(TRUE);
}

void MinGWMouseManager::SetSystemMousePosition(int32_t x, int32_t y)
{
	if(_windowHandle) {
		HWND hwnd = (HWND)_windowHandle;
		POINT point;
		point.x = x;
		point.y = y;
		ClientToScreen(hwnd, &point);
		SetCursorPos(point.x, point.y);
	}
}

void MinGWMouseManager::SetCursorImage(CursorImage cursor)
{
	HCURSOR hCursor = nullptr;
	
	switch(cursor) {
		case CursorImage::Arrow:
			hCursor = LoadCursor(nullptr, IDC_ARROW);
			break;
		case CursorImage::Cross:
			hCursor = LoadCursor(nullptr, IDC_CROSS);
			break;
		case CursorImage::Hidden:
			hCursor = nullptr;
			break;
	}
	
	SetCursor(hCursor);
}

double MinGWMouseManager::GetPixelScale()
{
	if(_windowHandle) {
		HWND hwnd = (HWND)_windowHandle;
		HDC hdc = GetDC(hwnd);
		int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
		ReleaseDC(hwnd, hdc);
		return dpi / 96.0;
	}
	return 1.0;
}
