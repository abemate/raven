#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <usp10.h>

// One run of text through Uniscribe as a list control lays it out: items, glyphs, advances, the widths of the characters and the visual
// order. The calls are answered without stopping the program, a whole array at a time; this checks that what comes back still adds up.
int raven_entry(void)
{
	static const WCHAR text[] = L"Stamp Tool, Add a stamp";
	const int length = (int)(sizeof(text) / sizeof(text[0])) - 1;
	HDC dc = CreateCompatibleDC(NULL);
	if (!dc) return 1;
	LOGFONTW definition = {0};
	definition.lfHeight = -16;
	lstrcpyW(definition.lfFaceName, L"Arial");
	HFONT font = CreateFontIndirectW(&definition);
	if (!font) return 2;
	HGDIOBJ previous = SelectObject(dc, font);
	if (!previous || SelectObject(dc, font) != font) return 3;
	SCRIPT_CACHE cache = NULL;
	SCRIPT_ITEM items[4];
	int item_count = 0;
	if (ScriptItemize(text, length, 3, NULL, NULL, items, &item_count) || item_count != 1 || items[1].iCharPos != length) return 10;
	WORD glyphs[64], clusters[64];
	SCRIPT_VISATTR attributes[64];
	int glyph_count = 0;
	if (ScriptShape(dc, &cache, text, length, 64, &items[0].a, glyphs, clusters, attributes, &glyph_count) || glyph_count != length) return 11;
	if (!cache) return 12;
	int advances[64];
	GOFFSET offsets[64];
	ABC abc = {7, 7, 7};
	for (int index = 0; index < 64; ++index) advances[index] = -1;
	// The cache alone stands for the device context from here on.
	if (ScriptPlace(NULL, &cache, glyphs, glyph_count, attributes, &items[0].a, advances, offsets, &abc)) return 13;
	int total = 0;
	for (int index = 0; index < glyph_count; ++index) {
		if (advances[index] <= 0 || offsets[index].du || offsets[index].dv) return 14;
		total += advances[index];
	}
	if (advances[glyph_count] != -1) return 15;
	if (abc.abcA || abc.abcC || (int)abc.abcB != total) return 16;
	int widths[64];
	for (int index = 0; index < 64; ++index) widths[index] = -1;
	if (ScriptGetLogicalWidths(&items[0].a, length, glyph_count, advances, clusters, attributes, widths)) return 17;
	int sum = 0;
	for (int index = 0; index < length; ++index) sum += widths[index];
	if (sum != total || widths[length] != -1 || widths[0] != advances[0]) return 18;
	BYTE levels[3] = {0, 0, 0};
	int visual[3] = {-1, -1, -1}, logical[3] = {-1, -1, -1};
	if (ScriptLayout(3, levels, visual, logical)) return 19;
	for (int index = 0; index < 3; ++index)
		if (visual[index] != index || logical[index] != index) return 20;
	SCRIPT_FONTPROPERTIES properties;
	properties.cBytes = sizeof(properties);
	properties.wgKashida = 0x1234;
	properties.iKashidaWidth = 99;
	if (ScriptGetFontProperties(dc, &cache, &properties) || properties.wgBlank != 0x20 || properties.wgDefault || properties.wgInvalid ||
		properties.wgKashida != 0xffff || properties.iKashidaWidth) return 21;
	SCRIPT_LOGATTR breaks[64];
	if (ScriptBreak(text, length, &items[0].a, breaks) || !breaks[0].fWordStop || !breaks[5].fWhiteSpace || !breaks[6].fWordStop) return 22;
	WCHAR lower[64];
	if (LCMapStringW(0x409, LCMAP_LOWERCASE, text, length + 1, lower, 64) != length + 1 || lower[0] != L's' || lower[6] != L't' || lower[length]) return 23;
	// A bitmap is selected with the program stopped (the served call declines it): the DC draws into the bitmap and hands back the old one.
	BITMAPINFO info = {0};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = 8;
	info.bmiHeader.biHeight = -8;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	void *bits = NULL;
	HBITMAP section = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0);
	if (!section || !bits) return 24;
	HGDIOBJ stock = SelectObject(dc, section);
	if (!stock || GetCurrentObject(dc, OBJ_BITMAP) != section) return 25;
	RECT square = {0, 0, 8, 8};
	HBRUSH brush = CreateSolidBrush(RGB(10, 20, 30));
	FillRect(dc, &square, brush);
	GdiFlush();
	if (((DWORD *)bits)[9] != 0x000a141eU) return 26;
	if (SelectObject(dc, stock) != section) return 27;
	DeleteObject(brush);
	DeleteObject(section);
	ScriptFreeCache(&cache);
	SelectObject(dc, previous);
	DeleteObject(font);
	DeleteDC(dc);
	return 0;
}
