#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// CreateDirectory of a folder that is there already fails with ERROR_ALREADY_EXISTS, also where the program may only read, as on Windows.
// PDF-XChange Editor makes each folder above the document it opens in turn; each was refused with ERROR_ACCESS_DENIED, and each was
// logged as a refused change in sandbox.log. A folder that is not there yet is still refused where the program may only read.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	SetLastError(0);
	CHECK(1, !CreateDirectoryW(L"Z:\\", 0) && GetLastError() == ERROR_ALREADY_EXISTS);
	SetLastError(0);
	CHECK(2, !CreateDirectoryW(L"C:\\Windows", 0) && GetLastError() == ERROR_ALREADY_EXISTS);
	CHECK(3, CreateDirectoryW(L"C:\\raven-existing-directory", 0));
	SetLastError(0);
	CHECK(4, !CreateDirectoryW(L"C:\\raven-existing-directory", 0) && GetLastError() == ERROR_ALREADY_EXISTS);
	CHECK(5, RemoveDirectoryW(L"C:\\raven-existing-directory"));
	return 0;
}
