#pragma once

#include <array>
#include <thread>
#include <atomic>

#if defined(__x86_64__) || defined(_M_X64)
#	define SPEEN_X86_64
#elif defined(__ARM_ARCH)
#	if __ARM_ARCH >= 6
#		define SPEEN_ARMV6
#	else
#		define SPEEN_ARM_OLD
#endif
#endif

#if defined(SPEEN_X86_64) ////////////////////////////////////////

#include <emmintrin.h>

namespace speen {
namespace detail {

inline auto pause() noexcept -> void { _mm_pause(); } 
inline auto post() noexcept -> void {} 

inline auto pause_long() noexcept -> void {
	pause(); pause(); pause(); pause(); pause();
	pause(); pause(); pause(); pause(); pause();
}

} // detail
} // speen

#elif defined(SPEEN_ARM_OLD) /////////////////////////////////////

namespace speen {
namespace detail {

inline auto pause() noexcept -> void { __asm__ volatile("yield"); } 
inline auto post() noexcept -> void {}

inline
auto pause_long() noexcept -> void {
	pause(); pause(); pause(); pause(); pause();
	pause(); pause(); pause(); pause(); pause();
}

} // detail
} // speen

#elif defined(SPEEN_ARMV6) //////////////////////////////////////

namespace speen {
namespace detail {

inline auto pause() noexcept -> void { __asm__ volatile("wfe" ::: "memory"); }
inline auto post() noexcept -> void { __asm__ volatile("sev" ::: "memory"); }

inline auto pause_long() noexcept -> void {
	// We are using WFE so just do one.
	pause();
}

} // detail
} // speen

#endif //////////////////////////////////////////////////////////

namespace speen {

namespace detail {

template <int ShortPauseCount, int LongPauseCount, typename PredFn> [[nodiscard]]
auto spin_wait_for_a_bit_with_backoff(PredFn pred) noexcept -> bool {
	if (pred()) { return true; }
	for (int i = 0; i < ShortPauseCount; i++) {
		pause();
		if (pred()) { return true; }
	}
	for (int i = 0; i < LongPauseCount; i++) {
		pause_long();
		if (pred()) { return true; }
	}
	return false;
}

template <int ShortPauseCount, typename PredFn> [[nodiscard]]
auto spin_wait_forever_with_backoff(PredFn pred) noexcept -> void {
	if (pred()) { return; }
	for (int i = 0; i < ShortPauseCount; i++) {
		pause();
		if (pred()) { return; }
	}
	for (;;) {
		pause_long();
		if (pred()) { return; }
	}
}

} // detail

auto pause() noexcept -> void {
	detail::pause();
}

auto post() noexcept -> void {
	detail::post();
}

template <typename PredFn>
auto wait_for_a_bit(PredFn pred) noexcept -> bool {
	return detail::spin_wait_for_a_bit_with_backoff<10, 3000>(pred);
}

template <typename PredFn>
auto wait(PredFn pred) noexcept -> void {
	detail::spin_wait_forever_with_backoff<10>(pred);
}

} // speen