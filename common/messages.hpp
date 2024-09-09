#pragma once

#include "c_render_mode.h"
#include "types.hpp"
#include "events.hpp"
#include <clap/id.h>
#include <variant>

namespace scuff::msg::in {

struct close_all_editors      {};
struct commit_changes         {};
struct device_create          { scuff::id::device dev; size_t callback; };
struct device_connect         { scuff::id::device out_dev; size_t out_port; scuff::id::device in_dev; size_t in_port; };
struct device_disconnect      { scuff::id::device out_dev; size_t out_port; scuff::id::device in_dev; size_t in_port; };
struct device_erase           { scuff::id::device dev; };
struct device_gui_hide        { scuff::id::device dev; };
struct device_gui_show        { scuff::id::device dev; };
struct device_set_render_mode { scuff::id::device dev; scuff_render_mode mode; };
struct event                  { scuff::id::device dev; scuff::events::event event; };
struct find_param             { scuff::id::device dev; size_t STR_param_id; size_t callback; };
struct get_param_value        { scuff::id::device dev; scuff::idx::param param; size_t callback; };
struct get_param_value_text   { scuff::id::device dev; scuff::idx::param param; double value; size_t callback; };
struct set_sample_rate        { double sr; };

using msg = std::variant<
	close_all_editors,
	commit_changes,
	device_create,
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

} // scuff::msg::in

namespace scuff::msg::out {

struct device_create_error     { scuff::id::device dev; size_t callback; };
struct device_create_success   { scuff::id::device dev; size_t callback; };
struct device_params_changed   { scuff::id::device dev; };
struct return_param            { scuff::idx::param param; size_t callback; };
struct return_param_value      { double value; size_t callback; };
struct return_param_value_text { size_t STR_text; size_t callback; };

using msg = std::variant<
	device_create_error,
	device_create_success,
	device_params_changed,
	return_param,
	return_param_value,
	return_param_value_text
>;

} // scuff::msg::out
