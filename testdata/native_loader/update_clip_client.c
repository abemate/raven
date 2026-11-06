#include <windows.h>

/* The update region stays inside the client area, as on Windows, whatever adds to it: a window that shrinks keeps only the part of
   its pending update that still lies in it, a child partly above its parent leaves only the visible part of its old place to
   repaint, and RedrawWindow or InvalidateRgn with an area reaching outside invalidate only what lies inside. Programs size their
   back buffer from the client rectangle and paint all of rcPaint (PDF-XChange Editor does), so a larger update ran them past the end
   of that buffer. */

static RECT painted;
static unsigned paints;

static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_PAINT) {
		PAINTSTRUCT ps;
		if (BeginPaint(window, &ps)) {
			painted = ps.rcPaint;
			++paints;
			EndPaint(window, &ps);
		}
		return 0;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

/* The window's update lies inside 0,0,width,height, and is that rectangle when expected is given. */
static int inside(HWND window, int width, int height, const RECT *expected)
{
	RECT update, box;
	BOOL pending = GetUpdateRect(window, &update, FALSE);
	HRGN region = CreateRectRgn(0, 0, 0, 0);
	int kind = GetUpdateRgn(window, region, FALSE);
	GetRgnBox(region, &box);
	DeleteObject(region);
	if (!expected)
		return !pending && kind == NULLREGION;
	if (!pending || kind == NULLREGION || kind == ERROR)
		return 0;
	if (update.left < 0 || update.top < 0 || update.right > width || update.bottom > height)
		return 0;
	if (box.left != update.left || box.top != update.top || box.right != update.right || box.bottom != update.bottom)
		return 0;
	return EqualRect(&update, expected);
}

int raven_entry(void)
{
	HINSTANCE instance = GetModuleHandleW(NULL);
	WNDCLASSW wc = {0};
	wc.lpfnWndProc = proc;
	wc.hInstance = instance;
	wc.lpszClassName = L"RavenUpdateClipFixture";
	if (!RegisterClassW(&wc))
		return 1;
	HWND window = CreateWindowExW(0, wc.lpszClassName, L"update clip", WS_POPUP, 50, 40, 200, 150, NULL, NULL, instance, NULL);
	if (!window)
		return 2;
	ShowWindow(window, SW_SHOW);
	UpdateWindow(window);
	ValidateRect(window, NULL);

	/* Pending update over the whole window, then the window shrinks: what is left is the new client area, and so is rcPaint. */
	InvalidateRect(window, NULL, FALSE);
	if (!SetWindowPos(window, NULL, 0, 0, 120, 90, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
		return 3;
	RECT client;
	GetClientRect(window, &client);
	if (client.right != 120 || client.bottom != 90)
		return 4;
	RECT whole = {0, 0, 120, 90};
	if (!inside(window, 120, 90, &whole))
		return 5;
	paints = 0;
	UpdateWindow(window);
	if (paints != 1 || !EqualRect(&painted, &whole))
		return 6;

	/* A child reaching above the top of its parent: hiding it leaves only the visible part of its place to repaint. */
	HWND child = CreateWindowExW(0, wc.lpszClassName, L"child", WS_CHILD | WS_VISIBLE, 10, -35, 50, 60, window, NULL, instance, NULL);
	if (!child)
		return 7;
	UpdateWindow(window);
	ValidateRect(window, NULL);
	ShowWindow(child, SW_HIDE);
	RECT uncovered = {10, 0, 60, 25};
	if (!inside(window, 120, 90, &uncovered))
		return 8;
	paints = 0;
	UpdateWindow(window);
	if (paints != 1 || !EqualRect(&painted, &uncovered))
		return 9;
	/* Destroying it, shown again, does the same. */
	ShowWindow(child, SW_SHOW);
	UpdateWindow(window);
	ValidateRect(window, NULL);
	if (!DestroyWindow(child))
		return 10;
	if (!inside(window, 120, 90, &uncovered))
		return 11;
	ValidateRect(window, NULL);

	/* RedrawWindow and InvalidateRgn with an area reaching outside the client area. */
	RECT across = {-20, -15, 30, 40}, part = {0, 0, 30, 40};
	if (!RedrawWindow(window, &across, NULL, RDW_INVALIDATE))
		return 12;
	if (!inside(window, 120, 90, &part))
		return 13;
	ValidateRect(window, NULL);
	RECT beyond = {500, 500, 600, 600};
	if (!RedrawWindow(window, &beyond, NULL, RDW_INVALIDATE))
		return 14;
	if (!inside(window, 120, 90, NULL))
		return 15;
	HRGN large = CreateRectRgn(-5, -5, 300, 300);
	if (!InvalidateRgn(window, large, FALSE))
		return 16;
	DeleteObject(large);
	if (!inside(window, 120, 90, &whole))
		return 17;
	ValidateRect(window, NULL);
	DestroyWindow(window);
	return 0;
}
