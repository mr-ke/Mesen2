#pragma once
#include "Shared/Interfaces/IMouseManager.h"

class MinGWMouseManager : public IMouseManager
{
private:
	void* _windowHandle;

public:
	MinGWMouseManager(void* windowHandle);
	virtual ~MinGWMouseManager();

	SystemMouseState GetSystemMouseState(void* rendererHandle) override;
	bool CaptureMouse(int32_t x, int32_t y, int32_t width, int32_t height, void* rendererHandle) override;
	void ReleaseMouse() override;
	void SetSystemMousePosition(int32_t x, int32_t y) override;
	void SetCursorImage(CursorImage cursor) override;
	double GetPixelScale() override;
};
