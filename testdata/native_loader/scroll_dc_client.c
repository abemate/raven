#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ScrollDC moves the bits of a device context's rectangle and names what the move uncovered, without invalidating anything. PDF-XChange
// Editor scrolls its page and thumbnail views with it.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	HDC dc = CreateCompatibleDC(0);
	CHECK(90, dc != 0);
	HBITMAP bitmap = CreateBitmap(100, 50, 1, 32, 0);
	CHECK(91, bitmap && SelectObject(dc, bitmap));
	RECT all = {0, 0, 100, 50};
	FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
	CHECK(92, SetPixel(dc, 10, 10, RGB(255, 0, 0)) != (COLORREF)-1);
	HRGN uncovered = CreateRectRgn(0, 0, 0, 0);
	RECT box;
	// The whole surface moves 5 to the right: the red pixel with it, and the 5 columns on the left are uncovered.
	CHECK(1, ScrollDC(dc, 5, 0, 0, 0, uncovered, &box));
	CHECK(2, GetPixel(dc, 15, 10) == RGB(255, 0, 0));
	CHECK(3, box.left == 0 && box.top == 0 && box.right == 5 && box.bottom == 50);
	RECT bounds;
	CHECK(4, GetRgnBox(uncovered, &bounds) != ERROR && bounds.left == 0 && bounds.right == 5 && bounds.bottom == 50);
	// Moved down by 3 inside a clip rectangle: the pixel lands at 13, and what is uncovered lies only within the clip rectangle.
	RECT clip = {0, 0, 50, 30};
	CHECK(5, ScrollDC(dc, 0, 3, 0, &clip, 0, &box));
	CHECK(6, GetPixel(dc, 15, 13) == RGB(255, 0, 0));
	CHECK(7, box.left == 0 && box.top == 0 && box.right == 50 && box.bottom == 3);
	// No move uncovers nothing.
	CHECK(8, ScrollDC(dc, 0, 0, 0, 0, 0, &box) && box.right == box.left);
	CHECK(9, !ScrollDC((HDC)(ULONG_PTR)0x7ff0, 1, 0, 0, 0, 0, 0));
	DeleteObject(uncovered);
	DeleteDC(dc);
	DeleteObject(bitmap);
	return 0;
}
