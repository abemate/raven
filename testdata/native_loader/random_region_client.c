#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// GetRandomRgn copies the regions a window's device context draws through.

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
	HDC dc = GetDC(window);
	HRGN region = CreateRectRgn(0, 0, 0, 0), left = CreateRectRgn(0, 0, 100, 100);
	CHECK(1, dc && GetRandomRgn(dc, region, SYSRGN) == 1);
	// No clipping region until one is selected.
	CHECK(2, GetRandomRgn(dc, region, 1) == 0);
	CHECK(3, SelectClipRgn(dc, left) != ERROR && GetRandomRgn(dc, region, 1) == 1);
	RECT box;
	CHECK(4, GetRgnBox(region, &box) != ERROR && box.left == 0 && box.right == 100 && box.bottom == 100);
	CHECK(5, GetRandomRgn(dc, region, 2) == 0 && GetRandomRgn(dc, region, 7) == -1);
	ReleaseDC(window, dc);
	DestroyWindow(window);
	return 0;
}
