#pragma once

#include "types.hpp"
#include "events.hpp"
#include "render_mode.hpp"
#include <clap/id.h>
#include <variant>

namespace tom::msg::in {

struct close_all_editors      {};
struct commit_changes         {};
struct device_add             { tom::id::device dev; };
struct device_connect         { tom::id::device out_dev; size_t out_port; tom::id::device in_dev; size_t in_port; };
struct device_disconnect      { tom::id::device out_dev; size_t out_port; tom::id::device in_dev; size_t in_port; };
struct device_erase           { tom::id::device dev; };
struct device_gui_hide        { tom::id::device dev; };
struct device_gui_show        { tom::id::device dev; };
struct device_set_render_mode { tom::id::device dev; tom::render_mode mode; };
struct event                  { tom::id::device dev; tom::events::event event; };
struct find_param             { tom::id::device dev; size_t STR_param_id; size_t callback; };
struct get_param_value        { tom::id::device dev; tom::idx::param param; size_t callback; };
struct get_param_value_text   { tom::id::device dev; tom::idx::param param; double value; size_t callback; };
struct set_sample_rate        { double sr; };

using msg = std::variant<
	close_all_editors,
	commit_changes,
	device_add,
	device_connect,
	device_disconnect,
	device_erase,
	device_gui_hide,
	device_gui_show,
	device_set_render_mode,
	event,
	find_param,
	get_param_value,
	get_param_value_text,
	set_sample_rate
>;

template <size_t N>
struct list {
	msg arr[N];
	size_t count = 0;
};

} // tom::msg::in

namespace tom::msg::out {

struct device_add_error        { tom::id::device dev; };
struct device_add_success      { tom::id::device dev; };
struct device_params_changed   { tom::id::device dev; };
struct return_param            { tom::idx::param param; size_t callback; };
struct return_param_value      { double value; size_t callback; };
struct return_param_value_text { size_t STR_text; size_t callback; };

using msg = std::variant<
	device_add_error,
	device_add_success,
	device_params_changed,
	return_param,
	return_param_value,
	return_param_value_text
>;

} // tom::msg::out
