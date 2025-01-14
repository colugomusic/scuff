#pragma once

#include "common-colors.hpp"
#include "common-device-info.hpp"
#include "common-param-info.hpp"
#include "common-plugin-type.hpp"
#include "common-render-mode.hpp"
#include <clap/id.h>
#include <deque>
#include <iterator>
#include <variant>
#include <vector>

namespace scuff::msg::in {

// These messages are sent from the client to a sandbox process.

struct activate               { double sr; };
struct close_all_editors      {};
struct crash                  {}; // Tell the sandbox process to crash. Important for testing.
struct deactivate             {};
struct device_connect         { int64_t out_dev_id; size_t out_port; int64_t in_dev_id; size_t in_port; };
struct device_create          { id::device::type dev_id; plugin_type type; std::string plugfile_path; std::string plugin_id; size_t callback; };
struct device_disconnect      { int64_t out_dev_id; size_t out_port; int64_t in_dev_id; size_t in_port; };
struct device_erase           { id::device::type dev_id; };
struct device_gui_hide        { id::device::type dev_id; };
struct device_gui_show        { id::device::type dev_id; };
struct device_load            { id::device::type dev_id; std::vector<std::byte> state; size_t callback; };
struct device_save            { id::device::type dev_id; size_t callback; };
struct event                  { id::device::type dev_id; scuff::event event; };
struct get_param_value        { id::device::type dev_id; size_t param_idx; size_t callback; };
struct get_param_value_text   { id::device::type dev_id; size_t param_idx; double value; size_t callback; };
struct heartbeat              {}; // Sandbox shuts itself down if this isn't received within a certain time.
struct panic				  {}; // "Panic" all devices.
struct set_render_mode        { render_mode mode; };
struct set_track_color        { id::device::type dev_id; std::optional<rgba32> color; };
struct set_track_name         { id::device::type dev_id; std::string name; };

using msg = std::variant<
	activate,
	close_all_editors,
	crash,
	deactivate,
	device_connect,
	device_create,
	device_disconnect,
	device_erase,
	device_gui_hide,
	device_gui_show,
	device_load,
	device_save,
	event,
	get_param_value,
	get_param_value_text,
	heartbeat,
	panic,
	set_render_mode,
	set_track_color,
	set_track_name
>;

} // scuff::msg::in

namespace scuff::msg::out {

// These messages are sent back from a sandbox process to the client.

struct confirm_activated             {};
struct device_create_fail            { id::device::type dev_id; std::string error; size_t callback; };
struct device_create_success         { id::device::type dev_id; std::string ports_shmid; size_t callback; };
struct device_editor_visible_changed { id::device::type dev_id; bool visible; int64_t native_handle; };
struct device_flags                  { id::device::type dev_id; int flags; };
struct device_port_info              { id::device::type dev_id; scuff::device_port_info info; };
struct device_latency                { id::device::type dev_id; uint32_t latency; };
struct device_load_fail              { id::device::type dev_id; std::string error; };
struct device_load_success           { id::device::type dev_id; };
struct device_param_info             { id::device::type dev_id; std::vector<client_param_info> info; };
struct report_error                  { std::string text; };
struct report_info                   { std::string text; };
struct report_warning                { std::string text; };
struct return_param_value            { double value; size_t callback; };
struct return_param_value_text       { std::string text; size_t callback; };
struct return_state                  { std::vector<std::byte> bytes; size_t callback; };

using msg = std::variant<
	confirm_activated,
	device_create_fail,
	device_create_success,
	device_editor_visible_changed,
	device_flags,
	device_port_info,
	device_latency,
	device_load_fail,
	device_load_success,
	device_param_info,
	report_error,
	report_info,
	report_warning,
	return_param_value,
	return_param_value_text,
	return_state
>;

using buf = std::vector<msg>;

} // scuff::msg::out

