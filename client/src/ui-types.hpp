#pragma once

#include "common-types.hpp"
#include <cs_plain_guarded.h>
#include <deque>
#include <string>
#include <variant>

namespace lg = libguarded;

namespace scuff {
namespace ui {
namespace msg {

struct device_create                 { create_device_result result; return_create_device_result callback; };
struct device_editor_visible_changed { id::device dev; bool visible; int64_t native_handle; };
struct device_flags_changed          { id::device dev; };
struct device_late_create            { create_device_result result; };
struct device_params_changed         { id::device dev; };
struct device_ports_changed          { id::device dev; };
struct device_state_load             { load_device_result result; };
struct error                         { std::string error; };
struct plugfile_broken               { id::plugfile plugfile; };
struct plugfile_scanned              { id::plugfile plugfile; };
struct plugin_broken                 { id::plugin plugin; };
struct plugin_scanned                { id::plugin plugin; };
struct return_device_state           { scuff::bytes state; return_bytes callback; };
struct return_param_value            { double value; return_double callback; };
struct return_param_value_text       { std::string text; return_string callback; };
struct sbox_crashed                  { id::sandbox sbox; std::string error; };
struct sbox_error                    { id::sandbox sbox; std::string error; };
struct sbox_info                     { id::sandbox sbox; std::string info; };
struct sbox_started                  { id::sandbox sbox; };
struct sbox_warning                  { id::sandbox sbox; std::string warning; };
struct scan_complete                 { };
struct scan_error                    { std::string error; };
struct scan_started                  { };
struct scan_warning                  { std::string warning; };

using general = std::variant<
	error,
	plugfile_broken,
	plugfile_scanned,
	plugin_broken,
	plugin_scanned,
	scan_complete,
	scan_error,
	scan_started,
	scan_warning
>;

using group = std::variant<
	device_create,
	device_editor_visible_changed,
	device_flags_changed,
	device_late_create,
	device_params_changed,
	device_ports_changed,
	device_state_load,
	error,
	return_device_state,
	return_param_value,
	return_param_value_text,
	sbox_crashed,
	sbox_error,
	sbox_info,
	sbox_started,
	sbox_warning
>;

template <typename T> using q = lg::plain_guarded<std::deque<T>>;
using general_q = q<general>;
using group_q   = q<group>;

} // msg
} // ui
} // scuff