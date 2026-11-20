#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellscalingapi.h>

// GetScaleFactorForMonitor answers the factor of 96 dots per inch.

#define CHECK(code, condition) do { if (!(condition)) return code; } while (0)

int raven_entry(void)
{
	HMONITOR monitor = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
	DEVICE_SCALE_FACTOR scale = 0;
	CHECK(1, GetScaleFactorForMonitor(monitor, &scale) == S_OK && scale == SCALE_100_PERCENT);
	CHECK(2, GetScaleFactorForMonitor(monitor, 0) == E_INVALIDARG);
	return 0;
}
