#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// GetGUIThreadInfo answers the input state, and only into a structure of the right size.

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
	GUITHREADINFO gui = {0};
	SetLastError(0);
	CHECK(1, !GetGUIThreadInfo(0, &gui) && GetLastError() == ERROR_INVALID_PARAMETER);
	gui.cbSize = sizeof(gui) + 4;
	CHECK(2, !GetGUIThreadInfo(0, &gui));
	gui.cbSize = sizeof(gui);
	CHECK(3, GetGUIThreadInfo(0, &gui));
	// A caret made in the window is reported with its window and rectangle.
	SetFocus(window);
	CHECK(4, CreateCaret(window, 0, 2, 12) && SetCaretPos(5, 6) && GetGUIThreadInfo(0, &gui));
	CHECK(5, gui.hwndCaret == window && gui.rcCaret.left == 5 && gui.rcCaret.top == 6 && gui.rcCaret.right == 7 && gui.rcCaret.bottom == 18);
	DestroyCaret();
	DestroyWindow(window);
	return 0;
}
