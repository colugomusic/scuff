#pragma once

#include <edwin.hpp>

namespace scuff::sbox {

struct window_size_f;
struct window_size_u32;

struct window_size_f {
	float width  = 0.0f;
	float height = 0.0f;
	window_size_f(float w, float h);
	window_size_f(const window_size_u32& u32);
	auto operator==(const window_size_f& other) const -> bool = default;
};

struct window_size_u32 {
	uint32_t width  = 0;
	uint32_t height = 0;
	window_size_u32(uint32_t w, uint32_t h);
	window_size_u32(const window_size_f& f);
	window_size_u32(const edwin::size& ez);
	auto operator==(const window_size_u32& other) const -> bool = default;
};

inline window_size_f::window_size_f(float w, float h) : width(w), height(h) {}
inline window_size_f::window_size_f(const window_size_u32& u32) : width(static_cast<float>(u32.width)), height(static_cast<float>(u32.height)) {}
inline window_size_u32::window_size_u32(uint32_t w, uint32_t h) : width(w), height(h) {}
inline window_size_u32::window_size_u32(const window_size_f& f) : width(static_cast<uint32_t>(f.width)), height(static_cast<uint32_t>(f.height)) {}
inline window_size_u32::window_size_u32(const edwin::size& ez) : width(static_cast<uint32_t>(ez.width)), height(static_cast<uint32_t>(ez.height)) {}

} // scuff::sbox
