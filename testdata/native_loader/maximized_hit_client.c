#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// A maximized window is not sized from its borders: the edges of the screen answer as the frame just inside them, so the top-right
// corner is the close button and the top edge the caption. Restored, the same window sizes from its borders again.
static LRESULT hit(HWND window,LONG x,LONG y)
{
	return SendMessageW(window,WM_NCHITTEST,0,MAKELPARAM(x,y));
}

int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=DefWindowProcW;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.lpszClassName=L"raven.maximized.hit";
	if (!RegisterClassW(&definition)) return 1;
	HWND window=CreateWindowExW(0,L"raven.maximized.hit",L"maximized",WS_OVERLAPPEDWINDOW,40,50,400,300,NULL,NULL,definition.hInstance,NULL);
	if (!window) return 2;
	ShowWindow(window,SW_SHOWMAXIMIZED);
	if (!IsZoomed(window)) return 3;
	RECT r;
	if (!GetWindowRect(window,&r)) return 4;
	const LONG middle_y=(r.top+r.bottom)/2,middle_x=(r.left+r.right)/2;
	if (hit(window,r.right-1,r.top)!=HTCLOSE) return 10;
	if (hit(window,r.right-1,r.top+3)!=HTCLOSE) return 11;
	if (hit(window,middle_x,r.top)!=HTCAPTION) return 12;
	const LRESULT left=hit(window,r.left,middle_y),right=hit(window,r.right-1,middle_y),bottom=hit(window,middle_x,r.bottom-1);
	if ((left>=HTLEFT && left<=HTBOTTOMRIGHT) || (right>=HTLEFT && right<=HTBOTTOMRIGHT) || (bottom>=HTLEFT && bottom<=HTBOTTOMRIGHT)) return 13;
	ShowWindow(window,SW_RESTORE);
	if (IsZoomed(window) || !GetWindowRect(window,&r)) return 5;
	if (hit(window,r.right-1,(r.top+r.bottom)/2)!=HTRIGHT || hit(window,r.left,(r.top+r.bottom)/2)!=HTLEFT) return 14;
	if (hit(window,r.right-1,r.top)!=HTTOPRIGHT) return 15;
	DestroyWindow(window);
	return 0;
}
