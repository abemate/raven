#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// The window that becomes active comes to the top of the stacking, with the windows it owns above it, as on Windows. Two PXC document
// windows lay over each other; the one clicked into stayed beneath, so WindowFromPoint named the other one and PXC moved the focus there.
int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=DefWindowProcW;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.lpszClassName=L"raven.active.stacking";
	if (!RegisterClassW(&definition)) return 1;
	HWND first=CreateWindowExW(0,L"raven.active.stacking",L"first",WS_OVERLAPPEDWINDOW,40,50,400,300,NULL,NULL,definition.hInstance,NULL);
	HWND second=CreateWindowExW(0,L"raven.active.stacking",L"second",WS_OVERLAPPEDWINDOW,40,50,400,300,NULL,NULL,definition.hInstance,NULL);
	if (!first || !second) return 2;
	ShowWindow(first,SW_SHOW);
	ShowWindow(second,SW_SHOW);
	POINT middle={400,320};
	if (GetActiveWindow()!=second) return 3;
	if (WindowFromPoint(middle)!=second) return 4;
	SetActiveWindow(first);
	if (GetActiveWindow()!=first) return 5;
	if (WindowFromPoint(middle)!=first) return 6;
	// A window the active one owns stays above it when its owner is activated again.
	HWND dialog=CreateWindowExW(0,L"raven.active.stacking",L"dialog",WS_POPUP | WS_CAPTION,100,120,200,150,first,NULL,definition.hInstance,NULL);
	if (!dialog) return 7;
	ShowWindow(dialog,SW_SHOWNOACTIVATE);
	POINT inside={200,195};
	SetActiveWindow(second);
	if (WindowFromPoint(middle)!=second) return 8;
	SetActiveWindow(first);
	if (GetActiveWindow()!=first) return 9;
	if (WindowFromPoint(inside)!=dialog) return 10;
	if (WindowFromPoint(middle)!=first) return 11;
	DestroyWindow(dialog);
	DestroyWindow(second);
	DestroyWindow(first);
	return 0;
}
