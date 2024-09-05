#pragma once

#include <compare>
#include <limits>
#include <string>

namespace tom::idx {

static constexpr auto INVALID = std::numeric_limits<size_t>::max();

struct param {
	size_t value = INVALID;
	explicit operator bool() const { return value != INVALID; }
	[[nodiscard]] auto operator<=>(const param& rhs) const = default;
};

} // tom::idx

namespace tom::id {

static constexpr auto INVALID = -1;

struct device   { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const device& rhs) const = default; };
struct sandbox  { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const sandbox& rhs) const = default; };
struct group    { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const group& rhs) const = default; };
struct plugin   { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const plugin& rhs) const = default; };
struct plugfile { int64_t value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const plugfile& rhs) const = default; };

} // tom::id

namespace tom::ext::id {

struct param  { std::string value; [[nodiscard]] auto operator<=>(const param& rhs) const = default; };
struct plugin { std::string value; [[nodiscard]] auto operator<=>(const plugin& rhs) const = default; };

} // tom::ext::id

namespace std {

template <> struct hash<tom::id::device>   { size_t operator()(const tom::id::device& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<tom::id::group>    { size_t operator()(const tom::id::group& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<tom::id::plugfile> { size_t operator()(const tom::id::plugfile& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<tom::id::plugin>   { size_t operator()(const tom::id::plugin& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<tom::id::sandbox>  { size_t operator()(const tom::id::sandbox& d) const { return std::hash<int64_t>{}(d.value); } };

} // std