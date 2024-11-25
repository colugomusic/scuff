#pragma once

#include "data.hpp"

namespace scuff::sbox::os {

[[nodiscard]] auto get_editor_window_native_handle(const sbox::device& dev) -> void*;
[[nodiscard]] auto make_clap_window_ref(View* view) -> clap_window_t;
auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void;
auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void;

} // scuff::sbox::os