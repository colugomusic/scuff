#pragma once

#include "common/types.hpp"
#include <cs_plain_guarded.h>
#include <deque>
#include <string>
#include <variant>

namespace lg = libguarded;

namespace scuff {
namespace report {
namespace msg {

struct device_error          { id::device dev; std::string error; };
struct device_params_changed { id::device dev; };
struct error                 { std::string error; };
struct plugfile_broken       { id::plugfile plugfile; };
struct plugfile_scanned      { id::plugfile plugfile; };
struct plugin_broken         { id::plugin plugin; };
struct plugin_scanned        { id::plugin plugin; };
struct sbox_crashed          { id::sandbox sbox; std::string error; };
struct sbox_error            { id::sandbox sbox; std::string error; };
struct sbox_info             { id::sandbox sbox; std::string info; };
struct sbox_started          { id::sandbox sbox; };
struct sbox_warning          { id::sandbox sbox; std::string warning; };
struct scan_complete         { };
struct scan_error            { std::string error; };
struct scan_started          { };
struct scan_warning          { std::string warning; };

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
	device_error,
	device_params_changed,
	error,
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
} // report
} // scuff