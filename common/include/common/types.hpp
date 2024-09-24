#pragma once

#include <compare>
#include <limits>
#include <string>

// TODO: sort all this out
typedef double      scuff_sample_rate;

// This refers to a .clap or .vst3 file, or a VST2 shared library.
typedef int64_t     scuff_plugfile;

// This refers to an instance of a plugin.
typedef int64_t     scuff_device;

// This refers to a group of sandboxes. Sandboxes all belong to a group.
typedef int64_t     scuff_group;

// This refers to a plugin.
typedef int64_t     scuff_plugin;

// This refers to a sandbox.
typedef int64_t     scuff_sbox;

// A device parameter index.
typedef size_t      scuff_param;

// A plugin parameter string id.
typedef const char* scuff_param_id;

// A plugin string id.
typedef const char* scuff_plugin_id;

namespace scuff::idx {

static constexpr auto INVALID = std::numeric_limits<size_t>::max();

struct param {
	size_t value = INVALID;
	explicit operator bool() const { return value != INVALID; }
	[[nodiscard]] auto operator<=>(const param& rhs) const = default;
};

} // scuff::idx

namespace scuff::id {

static constexpr auto INVALID = -1;

struct device   { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const device& rhs) const = default; };
struct sandbox  { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const sandbox& rhs) const = default; };
struct group    { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const group& rhs) const = default; };
struct plugin   { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const plugin& rhs) const = default; };
struct plugfile { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const plugfile& rhs) const = default; };

} // scuff::id

namespace scuff::ext::id {

struct param  { std::string value; [[nodiscard]] auto operator<=>(const param& rhs) const = default; };
struct plugin { std::string value; [[nodiscard]] auto operator<=>(const plugin& rhs) const = default; };

} // scuff::ext::id

namespace std {

template <> struct hash<scuff::id::device>   { size_t operator()(const scuff::id::device& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<scuff::id::group>    { size_t operator()(const scuff::id::group& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<scuff::id::plugfile> { size_t operator()(const scuff::id::plugfile& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<scuff::id::plugin>   { size_t operator()(const scuff::id::plugin& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<scuff::id::sandbox>  { size_t operator()(const scuff::id::sandbox& d) const { return std::hash<int64_t>{}(d.value); } };

} // std