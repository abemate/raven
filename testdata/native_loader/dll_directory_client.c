#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// GetDllDirectory gives back the folder SetDllDirectory added, or the size a buffer needs for it.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

static int same(const WCHAR *left, const WCHAR *right)
{
	unsigned i = 0;
	for (; right[i]; ++i) if (left[i] != right[i]) return 0;
	return !left[i];
}

int raven_entry(void)
{
	WCHAR folder[32];
	folder[0] = L'x';
	CHECK(1, GetDllDirectoryW(32, folder) == 0 && !folder[0]);
	CHECK(2, SetDllDirectoryW(L"C:\\Windows"));
	CHECK(3, GetDllDirectoryW(0, 0) == 11 && GetDllDirectoryW(10, folder) == 11);
	CHECK(4, GetDllDirectoryW(32, folder) == 10 && same(folder, L"C:\\Windows"));
	char narrow[32];
	CHECK(5, GetDllDirectoryA(32, narrow) == 10 && narrow[0] == 'C' && narrow[3] == 'W' && !narrow[10]);
	CHECK(6, SetDllDirectoryW(0) && GetDllDirectoryW(32, folder) == 0);
	return 0;
}
