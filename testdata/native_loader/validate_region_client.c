#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ValidateRgn takes a region off a window's update region, or the whole client area without one.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	WNDCLASSW type = {0};
	type.lpfnWndProc = DefWindowProcW;
	type.hInstance = GetModuleHandleW(0);
	type.lpszClassName = L"RavenTestWindow";
	CHECK(90, RegisterClassW(&type));
	HWND window = CreateWindowW(L"RavenTestWindow", L"test", WS_POPUP, 0, 0, 200, 100, 0, 0, type.hInstance, 0);
	CHECK(91, window != 0);
	RECT update;
	CHECK(1, InvalidateRect(window, 0, FALSE) && GetUpdateRect(window, &update, FALSE));
	HRGN left = CreateRectRgn(0, 0, 100, 100);
	CHECK(2, ValidateRgn(window, left) && GetUpdateRect(window, &update, FALSE) && update.left == 100 && update.right == 200);
	CHECK(3, ValidateRgn(window, 0) && !GetUpdateRect(window, &update, FALSE));
	SetLastError(0);
	CHECK(4, !ValidateRgn((HWND)(ULONG_PTR)0x7ff0, 0) && GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
	DestroyWindow(window);
	return 0;
}
