#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* PDF-XChange shows its progress window with SW_SHOWNOACTIVATE while the password dialog, which it owns, waits for input.
   Owned windows lie above their owners, so it must not come up on the host over it. It shows once the dialog is gone. */
static HWND main_window,progress,dialog;

static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
	if (message==WM_TIMER && wparam==1) {
		KillTimer(window,1);
		progress=CreateWindowExW(0,L"raven.covered",L"progress",WS_POPUP | WS_CAPTION,50,50,200,100,main_window,NULL,GetModuleHandleW(NULL),NULL);
		dialog=CreateWindowExW(0,L"raven.covered",L"dialog",WS_POPUP | WS_CAPTION,50,50,200,100,progress,NULL,GetModuleHandleW(NULL),NULL);
		if (!progress || !dialog) { PostQuitMessage(2); return 0; }
		ShowWindow(dialog,SW_SHOW);
		UpdateWindow(dialog);
		SetTimer(main_window,2,100,NULL);
		return 0;
	}
	if (message==WM_TIMER && wparam==2) {
		KillTimer(window,2);
		ShowWindow(progress,SW_SHOWNOACTIVATE);
		UpdateWindow(progress);
		SetTimer(main_window,3,300,NULL);
		return 0;
	}
	if (message==WM_TIMER && wparam==3) {
		KillTimer(window,3);
		DestroyWindow(dialog);
		SetTimer(main_window,4,300,NULL);
		return 0;
	}
	if (message==WM_TIMER && wparam==4) { KillTimer(window,4); DestroyWindow(main_window); return 0; }
	if (message==WM_DESTROY && window==main_window) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(window,message,wparam,lparam);
}

int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=procedure;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
	definition.lpszClassName=L"raven.covered";
	if (!RegisterClassW(&definition)) return 1;
	main_window=CreateWindowExW(0,L"raven.covered",L"main",WS_OVERLAPPEDWINDOW,100,100,300,200,NULL,NULL,definition.hInstance,NULL);
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
