#include <windows.h>

/* A child window hidden, destroyed or moved away uncovers the windows beneath it, which repaint that area as on Windows: PDF-XChange
   Editor's File view lies over the panes of the main window, and the Thumbnails pane under it kept the File view's pixels after it
   closed when only the parent was told to repaint. */

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_PAINT) {
		PAINTSTRUCT ps;
		if (BeginPaint(window, &ps))
			EndPaint(window, &ps);
		return 0;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

static void settle(HWND window)
{
	MSG message;
	UpdateWindow(window);
	while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
		DispatchMessageW(&message);
}

/* The pane's update after the cover leaves is the part the cover hid (pane coordinates). */
static int uncovered(HWND pane, int left, int top, int right, int bottom)
{
	RECT update;
	if (!GetUpdateRect(pane, &update, FALSE))
		return 0;
	return update.left <= left && update.top <= top && update.right >= right && update.bottom >= bottom;
}

int raven_entry(void)
{
	WNDCLASSW definition = {0};
	definition.lpfnWndProc = proc;
	definition.hInstance = GetModuleHandleW(NULL);
	definition.lpszClassName = L"raven.expose";
	if (!RegisterClassW(&definition))
		return 1;
	HWND main = CreateWindowExW(0, L"raven.expose", L"expose", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 40, 40, 400, 300, NULL, NULL,
		definition.hInstance, NULL);
	if (!main)
		return 2;
	/* A pane with a list inside it, and a cover above both reaching over part of them. */
	HWND pane = CreateWindowExW(0, L"raven.expose", L"pane", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 150, 250, main,
		NULL, definition.hInstance, NULL);
	HWND list = CreateWindowExW(0, L"raven.expose", L"list", WS_CHILD | WS_VISIBLE, 0, 50, 150, 200, pane, NULL, definition.hInstance, NULL);
	HWND cover = CreateWindowExW(0, L"raven.expose", L"cover", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 100, 200, main, NULL,
		definition.hInstance, NULL);
	if (!pane || !list || !cover)
		return 3;
	SetWindowPos(cover, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	ShowWindow(main, SW_SHOW);
	settle(main);
	ShowWindow(cover, SW_HIDE);
	if (!uncovered(pane, 0, 0, 100, 200))
		return 10;
	if (!uncovered(list, 0, 0, 100, 150))
		return 11;
	settle(main);
	/* Moving the cover away uncovers its old place too. */
	ShowWindow(cover, SW_SHOW);
	settle(main);
	SetWindowPos(cover, NULL, 200, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	if (!uncovered(list, 0, 0, 100, 150))
		return 12;
	settle(main);
	/* And destroying it. */
	SetWindowPos(cover, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	settle(main);
	DestroyWindow(cover);
	if (!uncovered(list, 0, 0, 100, 150))
		return 13;
	DestroyWindow(main);
	return 0;
}
