#pragma once

#include "c_events.h"
#include <variant>

namespace scuff::events {

using event = std::variant<
	scuff_event_param_gesture_begin,
	scuff_event_param_gesture_end,
	scuff_event_param_value
>;

} // scuff::events
