#pragma once

#if defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace scuff::os {

[[nodiscard]] static
auto get_process_id() -> int {
	return GetCurrentProcessId();
}

} // scuff::os

#elif defined(__APPLE__)

#include <unistd.h>

namespace scuff::os {

[[nodiscard]] static
auto get_process_id() -> int {
	return getpid();
}

} // scuff::os

#elif defined(__linux__)

#include <unistd.h>

namespace scuff::os {

[[nodiscard]] static
auto get_process_id() -> int {
	return getpid();
}

} // scuff::os

#endif
