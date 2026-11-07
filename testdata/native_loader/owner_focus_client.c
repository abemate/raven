#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Closing the active window gives the activation, and with it the keyboard, back to the window that owns it: a dialog destroyed or
// hidden leaves its owner active and focused, as on Windows.
int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=DefWindowProcW;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.lpszClassName=L"raven.owner.focus";
	if (!RegisterClassW(&definition)) return 1;
	HWND owner=CreateWindowExW(0,L"raven.owner.focus",L"owner",WS_OVERLAPPEDWINDOW,40,50,400,300,NULL,NULL,definition.hInstance,NULL);
	if (!owner) return 2;
	ShowWindow(owner,SW_SHOW);
	if (GetActiveWindow()!=owner || GetFocus()!=owner) return 3;
	HWND dialog=CreateWindowExW(WS_EX_TOOLWINDOW,L"raven.owner.focus",L"dialog",WS_POPUP | WS_CAPTION | WS_SYSMENU,80,90,200,150,owner,NULL,
		definition.hInstance,NULL);
	if (!dialog) return 4;
	ShowWindow(dialog,SW_SHOW);
	if (GetActiveWindow()!=dialog || GetFocus()!=dialog) return 5;
	DestroyWindow(dialog);
	if (GetActiveWindow()!=owner) return 10;
	if (GetFocus()!=owner) return 11;
	dialog=CreateWindowExW(0,L"raven.owner.focus",L"dialog",WS_POPUP | WS_CAPTION | WS_SYSMENU,80,90,200,150,owner,NULL,definition.hInstance,NULL);
	if (!dialog) return 6;
	ShowWindow(dialog,SW_SHOW);
	if (GetActiveWindow()!=dialog) return 7;
	ShowWindow(dialog,SW_HIDE);
	if (GetActiveWindow()!=owner) return 12;
	if (GetFocus()!=owner) return 13;
	DestroyWindow(dialog);
	DestroyWindow(owner);
	return 0;
}
