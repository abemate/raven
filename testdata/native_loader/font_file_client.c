#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// GetFontData hands out the whole font file as table 0, from any offset, and no collection of a single font.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	HDC dc = CreateCompatibleDC(0);
	CHECK(90, dc != 0);
	HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
	CHECK(91, font && SelectObject(dc, font));
	DWORD size = GetFontData(dc, 0, 0, 0, 0);
	CHECK(1, size != GDI_ERROR && size > 12);
	BYTE start[12];
	CHECK(2, GetFontData(dc, 0, 0, start, sizeof(start)) == sizeof(start));
	CHECK(3, (start[0] == 0 && start[1] == 1 && start[2] == 0 && start[3] == 0) || (start[0] == 'O' && start[1] == 'T') || start[0] == 't');
	CHECK(4, ((start[4] << 8) | start[5]) > 0);
	BYTE last[4];
	CHECK(5, GetFontData(dc, 0, size - 4, last, sizeof(last)) == sizeof(last));
	CHECK(6, GetFontData(dc, 0, size + 1, last, sizeof(last)) == GDI_ERROR);
	CHECK(7, GetFontData(dc, 0x66637474, 0, 0, 0) == GDI_ERROR);
	DWORD head = GetFontData(dc, 0x64616568, 0, 0, 0);
	CHECK(8, head != GDI_ERROR && head < size);
	DeleteDC(dc);
	DeleteObject(font);
	return 0;
}
