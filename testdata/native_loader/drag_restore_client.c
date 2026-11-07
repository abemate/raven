#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* A window created maximized with a size as large as the screen (PDF-XChange saves its size while maximized) is restored by the
   desktop without a size when its caption is dragged (KWin does this): it must come back smaller than it was maximized, or the drag
   leaves a normal window that still fills the screen, whose top edge then resizes it. */
static int result=100;
static RECT maximized_rect;

static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
	if (message==WM_SIZE && wparam==SIZE_MAXIMIZED) GetWindowRect(window,&maximized_rect);
	if (message==WM_SIZE && wparam==SIZE_RESTORED && maximized_rect.right>maximized_rect.left) {
		RECT now;
		GetWindowRect(window,&now);
		const int full=(maximized_rect.right-maximized_rect.left)*(maximized_rect.bottom-maximized_rect.top);
		const int area=(now.right-now.left)*(now.bottom-now.top);
		result=(GetWindowLongW(window,GWL_STYLE) & WS_MAXIMIZE) ? 10 : area>=full ? 11 : area<full/4 ? 12 : 0;
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
	definition.lpszClassName=L"raven.drag.restore";
	if (!RegisterClassW(&definition)) return 1;
	const int width=GetSystemMetrics(SM_CXSCREEN),height=GetSystemMetrics(SM_CYSCREEN);
	HWND window=CreateWindowExW(0,L"raven.drag.restore",L"restore",WS_OVERLAPPEDWINDOW | WS_MAXIMIZE,0,0,width,height,NULL,NULL,
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
