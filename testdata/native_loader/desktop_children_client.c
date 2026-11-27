#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// The top-level windows are the desktop window's children, as on Windows. PXC walks them from GetWindow(GetDesktopWindow(), GW_CHILD)
// to find the toolbar under the pointer while a command is dragged from Customize Toolbars; Raven answered no window, so nothing
// took the drop.
int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=DefWindowProcW;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.lpszClassName=L"raven.desktop.children";
	if (!RegisterClassW(&definition)) return 1;
	HWND first=CreateWindowExW(0,L"raven.desktop.children",L"first",WS_OVERLAPPEDWINDOW,40,50,400,300,NULL,NULL,definition.hInstance,NULL);
	HWND second=CreateWindowExW(0,L"raven.desktop.children",L"second",WS_OVERLAPPEDWINDOW,80,90,400,300,NULL,NULL,definition.hInstance,NULL);
	if (!first || !second) return 2;
	ShowWindow(first,SW_SHOW);
	ShowWindow(second,SW_SHOW);
	HWND desktop=GetDesktopWindow();
	if (!desktop) return 3;
	// The walk from the desktop's first child meets both windows, the active one first, and never the desktop itself.
	BOOL saw_first=FALSE,saw_second=FALSE;
	unsigned count=0;
	HWND top=GetWindow(desktop,GW_CHILD);
	if (top!=second) return 4;
	for (HWND window=top;window && count<1000;window=GetWindow(window,GW_HWNDNEXT),++count) {
		if (window==desktop) return 5;
		if (window==first) saw_first=TRUE;
		if (window==second) saw_second=TRUE;
	}
	if (!saw_first || !saw_second) return 6;
	if (GetTopWindow(desktop)!=second || GetTopWindow(NULL)!=second) return 7;
	if (FindWindowExW(desktop,NULL,L"raven.desktop.children",L"first")!=first) return 8;
	if (FindWindowExW(NULL,NULL,L"raven.desktop.children",L"second")!=second) return 9;
	if (GetWindow(second,GW_HWNDNEXT)==desktop || GetWindow(first,GW_HWNDPREV)==desktop) return 10;
	DestroyWindow(second);
	DestroyWindow(first);
	return 0;
}
