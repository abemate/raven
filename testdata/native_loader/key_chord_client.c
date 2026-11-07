#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// The host presses Ctrl+Alt+7 and then Alt+F. As on Windows, Ctrl+Alt+7 comes as WM_KEYDOWN (a program's Ctrl+Alt shortcuts look for
// that, and treat system keys as menu navigation) and types no character, with left Ctrl and left Alt down but not right Alt, while Alt+F stays a system key with its WM_SYSCHAR.
static int result=100;
static int seven_down,seven_system,seven_character,f_system,f_character,f_plain,sides_wrong;

static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
	if (message==WM_KEYDOWN && wparam=='7') {
		++seven_down;
		// Left Ctrl and left Alt are down; the right ones (right Alt is AltGr) are not.
		if (!(GetKeyState(VK_LCONTROL) & 0x8000) || !(GetKeyState(VK_LMENU) & 0x8000) || (GetKeyState(VK_RMENU) & 0x8000) ||
			(GetKeyState(VK_RCONTROL) & 0x8000) || !(GetKeyState(VK_CONTROL) & 0x8000) || !(GetKeyState(VK_MENU) & 0x8000)) ++sides_wrong;
	}
	if (message==WM_SYSKEYDOWN && wparam=='7') ++seven_system;
	if (message==WM_CHAR && wparam=='7') ++seven_character;
	if (message==WM_SYSKEYDOWN && wparam=='F') ++f_system;
	if (message==WM_KEYDOWN && wparam=='F') ++f_plain;
	if (message==WM_SYSCHAR && (wparam=='f' || wparam=='F')) { ++f_character; return 0; }
	if (message==WM_CLOSE) {
		result=seven_system ? 10 : !seven_down ? 11 : seven_character ? 12 : sides_wrong ? 16 : f_plain ? 13 : !f_system ? 14 : !f_character ? 15 : 0;
		DestroyWindow(window);
		return 0;
	}
	if (message==WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(window,message,wparam,lparam);
}

int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=procedure;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
	definition.lpszClassName=L"raven.key.chord";
	if (!RegisterClassW(&definition)) return 1;
	HWND window=CreateWindowExW(0,L"raven.key.chord",L"chord",WS_OVERLAPPEDWINDOW,40,50,300,200,NULL,NULL,definition.hInstance,NULL);
	if (!window) return 2;
	ShowWindow(window,SW_SHOW);
	UpdateWindow(window);
	SetFocus(window);
	MSG message;
	while (GetMessageW(&message,NULL,0,0)>0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	return result;
}
