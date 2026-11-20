#pragma once

#include <chrono>
#include <cstdlib>
#include <unistd.h>

namespace raven {

// RAVEN_TRACE_INPUT_ON names a file: while it exists, clicks, keys, focus and capture are traced to stderr, so a fault that shows up
// only after hours of use can be followed in the session that has it. The file is looked for at most ten times a second.
inline bool input_trace_on()
{
	static const char *const path=std::getenv("RAVEN_TRACE_INPUT_ON");
	if (!path || !*path) return false;
	static auto checked=std::chrono::steady_clock::time_point{};
	static bool on=false;
	if (const auto now=std::chrono::steady_clock::now(); now-checked>std::chrono::milliseconds(100)) {
		checked=now;
		on=::access(path,F_OK)==0;
	}
	return on;
}

// Milliseconds on the steady clock, the clock Raven stamps guest messages with.
inline long long input_trace_now()
{
	return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}
