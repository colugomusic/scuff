#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace scuff::sbox::ezwin {

struct window;
struct native_handle { void* value = nullptr; };
struct position      { int x = 0; int y = 0; }; 
struct resizable     { bool value = false; };
struct size          { int width  = 0; int height = 0; }; 
struct title         { std::string_view value; };
struct icon32        { ezwin::size size; std::span<uint32_t> pixels; };
struct visible       { bool value = false; };

namespace sig {
using on_window_closed  = void();
using on_window_resized = void(ezwin::size size);
} // sig

namespace fn {
using on_window_closed  = std::function<sig::on_window_closed>;
using on_window_resized = std::function<sig::on_window_resized>;
} // fn

struct window_config {
	ezwin::fn::on_window_closed on_closed;
	ezwin::fn::on_window_resized on_resized;
	ezwin::icon32 icon;
	ezwin::native_handle parent;
	ezwin::position position;
	ezwin::resizable resizable;
	ezwin::size size;
	ezwin::title title;
	ezwin::visible visible;
};

[[nodiscard]] auto create(window_config cfg) -> window*;
              auto destroy(window* w) -> void;
[[nodiscard]] auto get_native_handle(const window& w) -> native_handle;
              auto set(window* w, ezwin::icon32 icon) -> void;
              auto set(window* w, ezwin::position position) -> void;
              auto set(window* w, ezwin::position position, ezwin::size size) -> void;
              auto set(window* w, ezwin::resizable resizable) -> void;
              auto set(window* w, ezwin::size size) -> void;
              auto set(window* w, ezwin::title title) -> void;
              auto set(window* w, ezwin::visible visible) -> void;
              auto set(window* w, fn::on_window_closed cb) -> void;
              auto set(window* w, fn::on_window_resized cb) -> void;
              auto process_messages() -> void;

} // scuff::sbox::ezwin