#pragma once

#include <compare>
#include <limits>
#include <string>

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

struct device   { using type = int64_t; type value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const device& rhs) const = default; };
struct sandbox  { using type = int64_t; type value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const sandbox& rhs) const = default; };
struct group    { using type = int64_t; type value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const group& rhs) const = default; };
struct plugin   { using type = int64_t; type value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const plugin& rhs) const = default; };
struct plugfile { using type = int64_t; type value = INVALID; explicit operator bool() const { return value != INVALID; } auto operator<=>(const plugfile& rhs) const = default; };

} // scuff::id

namespace scuff::ext::id {

struct param  { uint32_t value = std::numeric_limits<uint32_t>::max(); [[nodiscard]] auto operator<=>(const param& rhs) const = default; };
struct plugin { std::string value; [[nodiscard]] auto operator<=>(const plugin& rhs) const = default; };

} // scuff::ext::id

namespace std {

template <> struct hash<scuff::id::device>   { size_t operator()(const scuff::id::device& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<scuff::id::group>    { size_t operator()(const scuff::id::group& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<scuff::id::plugfile> { size_t operator()(const scuff::id::plugfile& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<scuff::id::plugin>   { size_t operator()(const scuff::id::plugin& d) const { return std::hash<int64_t>{}(d.value); } };
template <> struct hash<scuff::id::sandbox>  { size_t operator()(const scuff::id::sandbox& d) const { return std::hash<int64_t>{}(d.value); } };

} // std