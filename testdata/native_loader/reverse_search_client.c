#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>

// StrRStrI finds the last match before an end, ignoring case.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	const WCHAR *text = L"abcABCabc";
	CHECK(1, StrRStrIW(text, 0, L"BC") == text + 7);
	CHECK(2, StrRStrIW(text, text + 7, L"bc") == text + 4);
	CHECK(3, StrRStrIW(text, text + 6, L"BCA") == text + 4);
	CHECK(4, !StrRStrIW(text, 0, L"") && !StrRStrIW(text, 0, L"xyz"));
	const char *letters = "abcABCabc";
	CHECK(5, StrRStrIA(letters, 0, "BC") == letters + 7 && StrRStrIA(letters, letters + 2, "AB") == letters);
	return 0;
}
