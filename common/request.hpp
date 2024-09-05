#pragma once

namespace tom {

struct request_id {
	static constexpr auto INVALID = int64_t(-1);
	int64_t value = INVALID;
	explicit operator bool() const { return value != INVALID; }
	[[nodiscard]] auto operator<=>(request_id rhs) const -> std::strong_ordering { return value <=> rhs.value; }
};

} // tom
