#pragma once

#include "data.hpp"

namespace scuff::sbox::os {

auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void;
auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void;

} // scuff::sbox::os