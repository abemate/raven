#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <icm.h>

// GetColorDirectory answers this machine's colour profile folder, after telling a caller the size it needs.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

static int same(const WCHAR *left, const WCHAR *right)
{
	unsigned i = 0;
	for (; right[i]; ++i) if (left[i] != right[i]) return 0;
	return !left[i];
}

static int ends_with(const WCHAR *text, const WCHAR *end)
{
	unsigned length = 0, tail = 0;
	while (text[length]) ++length;
	while (end[tail]) ++tail;
	return length >= tail && same(text + length - tail, end);
}

int raven_entry(void)
{
	DWORD size = 0;
	WCHAR color[MAX_PATH];
	SetLastError(0);
	CHECK(1, !GetColorDirectoryW(0, 0, &size) && GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 2 && size <= sizeof(color));
	CHECK(2, GetColorDirectoryW(0, color, &size) && ends_with(color, L"\\spool\\drivers\\color"));
	SetLastError(0);
	CHECK(3, !GetColorDirectoryW(0, color, 0) && GetLastError() == ERROR_INVALID_PARAMETER);
	char narrow[MAX_PATH];
	size = sizeof(narrow);
	CHECK(4, GetColorDirectoryA(0, narrow, &size) && narrow[0] == 'C' && narrow[1] == ':');
	return 0;
}
