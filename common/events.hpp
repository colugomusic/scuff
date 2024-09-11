#pragma once

#include "c_events.h"
#include "visit.hpp"
#include <concepts>
#include <string_view>
#include <variant>
#include <vector>

namespace concepts {
template<typename T> concept is_arithmetic = std::is_arithmetic_v<T>;
template<typename T> concept is_enum       = std::is_enum_v<T>;
} // concepts

template <typename T> requires concepts::is_arithmetic<T> || concepts::is_enum<T>
auto serialize(T value, std::vector<std::byte>* bytes) -> void {
	const auto offset = bytes->size();
	bytes->resize(offset + sizeof(T));
	const auto dest = reinterpret_cast<T*>(bytes->data() + offset);
	*dest = value;
}

inline
auto serialize(std::string_view value, std::vector<std::byte>* bytes) -> void {
	::serialize(value.size(), bytes);
	const auto offset = bytes->size();
	bytes->resize(offset + (value.size() * sizeof(char)));
	const auto dest = reinterpret_cast<char*>(bytes->data() + offset);
	std::copy(value.begin(), value.end(), dest);
}

namespace scuff::events {

using event = std::variant<
	scuff_event_param_gesture_begin,
	scuff_event_param_gesture_end,
	scuff_event_param_value
>;

[[nodiscard]] inline
auto size_of(const event& e) -> size_t {
	return fast_visit([](const auto& e) { return sizeof(e); }, e);
}

inline
auto serialize_(const scuff_event_param_gesture_begin& msg, std::vector<std::byte>* bytes) -> void {
	const auto size   = size_of(msg);
	const auto offset = bytes->size();
	bytes->resize(offset + size);
	const auto dest = reinterpret_cast<scuff_event_param_gesture_begin*>(bytes->data() + offset);
	*dest = msg;
}

inline
auto serialize_(const scuff_event_param_gesture_end& msg, std::vector<std::byte>* bytes) -> void {
	const auto size   = size_of(msg);
	const auto offset = bytes->size();
	bytes->resize(offset + size);
	const auto dest = reinterpret_cast<scuff_event_param_gesture_end*>(bytes->data() + offset);
	*dest = msg;
}

inline
auto serialize_(const scuff_event_param_value& msg, std::vector<std::byte>* bytes) -> void {
	const auto size   = size_of(msg);
	const auto offset = bytes->size();
	bytes->resize(offset + size);
	const auto dest = reinterpret_cast<scuff_event_param_value*>(bytes->data() + offset);
	*dest = msg;
}

inline
auto serialize(const event& msg, std::vector<std::byte>* bytes) -> void {
	fast_visit([bytes](const auto& e) { serialize_(e, bytes); }, msg);
}

} // scuff::events
