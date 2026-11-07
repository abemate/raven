#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* PDF-XChange's Alt key tips are unowned WS_POPUP tool windows placed in screen coordinates next to each ribbon button. They must show
   where the program put them, on the active window, not as windows of their own that the compositor places. */
static HWND main_window;

static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
	if (message==WM_TIMER && wparam==1) {
		KillTimer(window,1);
		RECT outer;
		GetWindowRect(main_window,&outer);
		HWND tip=CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,L"raven.key.tip",L"",WS_POPUP | WS_DISABLED,
			0,0,20,14,NULL,NULL,GetModuleHandleW(NULL),NULL);
		if (!tip) { PostQuitMessage(2); return 0; }
		SetWindowPos(tip,HWND_TOPMOST,outer.left+30,outer.top+40,20,14,SWP_NOACTIVATE);
		ShowWindow(tip,SW_SHOWNA);
		UpdateWindow(tip);
		SetTimer(main_window,2,200,NULL);
		return 0;
	}
	if (message==WM_TIMER && wparam==2) { KillTimer(window,2); DestroyWindow(main_window); return 0; }
	if (message==WM_DESTROY && window==main_window) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(window,message,wparam,lparam);
}

int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=procedure;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
	definition.lpszClassName=L"raven.key.tip";
	if (!RegisterClassW(&definition)) return 1;
	main_window=CreateWindowExW(0,L"raven.key.tip",L"main",WS_OVERLAPPEDWINDOW,100,100,300,200,NULL,NULL,definition.hInstance,NULL);
	if (!main_window) return 1;
	ShowWindow(main_window,SW_SHOW);
	UpdateWindow(main_window);
	SetTimer(main_window,1,200,NULL);
	MSG message;
	while (GetMessageW(&message,NULL,0,0)>0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	return (int)message.wParam;
}
