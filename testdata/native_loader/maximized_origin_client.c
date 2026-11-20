#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* A window created maximized at its saved position (PDF-XChange starts this way) and given its size by the desktop fills the screen from
   its corner: kept at the saved position it reaches past the bottom of the screen, and the program lays itself out short by that much. */
static int result=100;

static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
	if (message==WM_SIZE && wparam==SIZE_MAXIMIZED && LOWORD(lparam)>500) {
		RECT now;
		GetWindowRect(window,&now);
		result=now.left || now.top ? 13 : now.right!=800 || now.bottom!=600 ? 14 : 0;
	}
	if (message==WM_CLOSE) { DestroyWindow(window); return 0; }
	if (message==WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(window,message,wparam,lparam);
}

int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=procedure;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
	definition.lpszClassName=L"raven.maximized.origin";
	if (!RegisterClassW(&definition)) return 1;
	HWND window=CreateWindowExW(0,L"raven.maximized.origin",L"origin",WS_OVERLAPPEDWINDOW | WS_MAXIMIZE,40,130,500,300,NULL,NULL,
		definition.hInstance,NULL);
	if (!window) return 2;
	ShowWindow(window,SW_SHOWMAXIMIZED);
	UpdateWindow(window);
	MSG message;
	while (GetMessageW(&message,NULL,0,0)>0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	return result;
}
