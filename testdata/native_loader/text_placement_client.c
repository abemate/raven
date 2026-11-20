#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <usp10.h>

// GetCharacterPlacement, GetFontLanguageInfo and ScriptGetLogicalWidths lay text out as the other text calls do.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	HDC memory = CreateCompatibleDC(0);
	HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
	CHECK(1, memory && font && SelectObject(memory, font));
	// One glyph per character: the glyphs GetGlyphIndices gives, and advances that add up to the width answered.
	WCHAR glyphs[8] = {0}, indices[8] = {0};
	int advances[8] = {0};
	GCP_RESULTSW results = {0};
	results.lStructSize = sizeof(results);
	results.lpGlyphs = glyphs;
	results.lpDx = advances;
	results.nGlyphs = 5;
	const DWORD placed = GetCharacterPlacementW(memory, L"Hello", 5, 0, &results, 0);
	CHECK(2, placed && results.nGlyphs == 5);
	CHECK(3, GetGlyphIndicesW(memory, L"Hello", 5, indices, 0) == 5);
	int total = 0;
	for (int i = 0; i < 5; ++i) {
		CHECK(4, glyphs[i] == indices[i] && advances[i] > 0);
		total += advances[i];
	}
	CHECK(5, total == (int)LOWORD(placed));
	// GCP_MAXEXTENT stops the fit at the first character past the extent.
	results.nGlyphs = 5;
	CHECK(6, GetCharacterPlacementW(memory, L"Hello", 5, advances[0], &results, GCP_MAXEXTENT) && results.nMaxFit == 1);
	CHECK(7, GetFontLanguageInfo(memory) != GCP_ERROR && GetFontLanguageInfo((HDC)(ULONG_PTR)0x7ff4) == GCP_ERROR);
	// The second and third characters make one glyph, whose advance they share.
	SCRIPT_ANALYSIS analysis = {0};
	int glyph_advances[3] = {10, 20, 30}, widths[4] = {0};
	WORD clusters[4] = {0, 1, 1, 2};
	SCRIPT_VISATTR visual[3] = {0};
	CHECK(8, ScriptGetLogicalWidths(&analysis, 4, 3, glyph_advances, clusters, visual, widths) == S_OK);
	CHECK(9, widths[0] == 10 && widths[1] == 10 && widths[2] == 10 && widths[3] == 30);
	CHECK(10, ScriptGetLogicalWidths(&analysis, 0, 3, glyph_advances, clusters, visual, widths) == E_INVALIDARG);
	DeleteObject(font);
	DeleteDC(memory);
	return 0;
}
