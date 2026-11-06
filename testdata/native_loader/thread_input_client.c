#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// The main thread shows a framed window and then waits on an event without pumping messages, as a program does while a dialog of
// another thread is open. The worker thread shows its own window and pumps. The host moves the pointer over the main window first and
// then clicks the worker's: the move must wait in the main thread's queue, not hold up the worker until the main thread answers it.
static HANDLE clicked;

static LRESULT CALLBACK worker_procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
	if (message==WM_LBUTTONDOWN) { SetEvent(clicked); PostQuitMessage(0); return 0; }
	return DefWindowProcW(window,message,wparam,lparam);
}

static DWORD WINAPI worker(void *parameter)
{
	(void)parameter;
	HWND window=CreateWindowExW(0,L"raven.thread.input.worker",L"worker",WS_OVERLAPPEDWINDOW,400,50,200,150,NULL,NULL,
		GetModuleHandleW(NULL),NULL);
	if (!window) return 1;
	ShowWindow(window,SW_SHOW);
	UpdateWindow(window);
	MSG message;
	while (GetMessageW(&message,NULL,0,0)>0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	DestroyWindow(window);
	return 0;
}

int raven_entry(void)
{
	WNDCLASSW definition={0};
	definition.lpfnWndProc=DefWindowProcW;
	definition.hInstance=GetModuleHandleW(NULL);
	definition.hbrBackground=(HBRUSH)GetStockObject(WHITE_BRUSH);
	definition.lpszClassName=L"raven.thread.input.main";
	if (!RegisterClassW(&definition)) return 1;
	definition.lpfnWndProc=worker_procedure;
	definition.lpszClassName=L"raven.thread.input.worker";
	if (!RegisterClassW(&definition)) return 2;
	HWND window=CreateWindowExW(0,L"raven.thread.input.main",L"main",WS_OVERLAPPEDWINDOW,40,50,300,200,NULL,NULL,definition.hInstance,NULL);
	if (!window) return 3;
	ShowWindow(window,SW_SHOW);
	UpdateWindow(window);
	clicked=CreateEventW(NULL,TRUE,FALSE,NULL);
	if (!clicked) return 4;
	HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);
	if (!thread) return 5;
	// The worker gets its click within moments unless it is stuck waiting for this thread.
	if (WaitForSingleObject(clicked,6000)!=WAIT_OBJECT_0) return 10;
	if (WaitForSingleObject(thread,6000)!=WAIT_OBJECT_0) return 11;
	DWORD code=1;
	GetExitCodeThread(thread,&code);
	DestroyWindow(window);
	return code ? 12 : 0;
}
