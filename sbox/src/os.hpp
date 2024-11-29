#pragma once

#include "data.hpp"

namespace scuff::sbox::os {

[[nodiscard]] auto make_clap_window_ref(edwin::window* wnd) -> clap_window_t;
auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void;
auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void;

} // scuff::sbox::os