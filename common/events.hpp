#pragma once

#include "c_events.h"
#include "serialization.hpp"
#include "visit.hpp"
#include <string_view>
#include <variant>

namespace scuff {

using event = std::variant<
	scuff_event_param_gesture_begin,
	scuff_event_param_gesture_end,
	scuff_event_param_value
>;

} // scuff

static
auto deserialize(std::span<const std::byte>* bytes, scuff::event* e) -> void {
	deserialize(bytes, e, "scuff::event");
}
