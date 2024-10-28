#pragma once

#include "data.hpp"

namespace scuff::sbox::os {

[[nodiscard]] auto get_editor_window_native_handle(const sbox::device& dev) -> void*;
auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void;
auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void;

} // scuff::sbox::os