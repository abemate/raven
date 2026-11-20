#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* A window destroyed with its timer still running takes the timer with it, as on Windows. Kept, such timers filled the table in a long
   session until no program could start a timer. 1100 windows each start one and are destroyed; every SetTimer must succeed. */
int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=DefWindowProcW;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.lpszClassName=L"raven.destroyed.timers";
	if (!RegisterClassW(&definition)) return 1;
	for (int index=0;index<1100;++index) {
		HWND window=CreateWindowExW(0,L"raven.destroyed.timers",L"timers",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,definition.hInstance,NULL);
		if (!window) return 2;
		if (!SetTimer(window,7,60000,NULL)) return 3;
		if (!DestroyWindow(window)) return 4;
	}
	/* With the table full a new timer fails, but setting one that exists again replaces it and still works. */
	HWND window=CreateWindowExW(0,L"raven.destroyed.timers",L"timers",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,definition.hInstance,NULL);
	if (!window) return 5;
	for (int index=0;index<1100;++index)
		if (!SetTimer(window,(UINT_PTR)index+1,60000,NULL)) return index<1000 ? 6 : SetTimer(window,1,30000,NULL) ? 0 : 8;
	return 7;
}
