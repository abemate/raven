#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// GetSystemTimeAdjustment answers a clock nobody adjusts, and leaves out what it is not asked for.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	DWORD adjustment = 0, increment = 0;
	BOOL disabled = FALSE;
	CHECK(1, GetSystemTimeAdjustment(&adjustment, &increment, &disabled));
	CHECK(2, disabled && increment && adjustment == increment);
	return 0;
}
