#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Run in testdata/timezone/lasting-daylight.tzif, a zone that keeps daylight time from 2026 on as America/Vancouver does. Its 2026
// daylight time never ends, which Windows describes with a change back at the year's last instant; from 2027 it has none. Whatever the
// year, the zone described and the clock agree: the bias GetTimeZoneInformation says is in force is the one GetLocalTime keeps.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

static LONGLONG ticks(const SYSTEMTIME *time)
{
	FILETIME file;
	SystemTimeToFileTime(time, &file);
	return (LONGLONG)file.dwHighDateTime << 32 | file.dwLowDateTime;
}

int raven_entry(void)
{
	TIME_ZONE_INFORMATION zone;
	CHECK(1, GetTimeZoneInformationForYear(2026, 0, &zone));
	CHECK(2, zone.Bias == 480 && zone.DaylightBias == -60);
	CHECK(3, zone.DaylightDate.wMonth == 3 && zone.DaylightDate.wDay == 2 && zone.DaylightDate.wDayOfWeek == 0 && zone.DaylightDate.wHour == 2);
	CHECK(4, zone.StandardDate.wMonth == 12 && zone.StandardDate.wDay == 5 && zone.StandardDate.wDayOfWeek == 4 &&
		zone.StandardDate.wHour == 23 && zone.StandardDate.wMinute == 59 && zone.StandardDate.wSecond == 59 && zone.StandardDate.wMilliseconds == 999);
	CHECK(5, GetTimeZoneInformationForYear(2027, 0, &zone));
	CHECK(6, zone.Bias == 420 && !zone.DaylightDate.wMonth && !zone.StandardDate.wMonth);
	const DWORD kind = GetTimeZoneInformation(&zone);
	SYSTEMTIME utc, local;
	GetSystemTime(&utc);
	GetLocalTime(&local);
	// Minutes the clock is behind UTC, to the nearest quarter hour.
	const LONG behind = (LONG)((ticks(&utc) - ticks(&local) + 4500000000LL) / 9000000000LL) * 15;
	CHECK(7, kind == TIME_ZONE_ID_DAYLIGHT ? zone.Bias + zone.DaylightBias == behind : zone.Bias == behind);
	CHECK(8, utc.wYear != 2026 || kind == TIME_ZONE_ID_DAYLIGHT);
	return 0;
}
