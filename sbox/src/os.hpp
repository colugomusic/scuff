#pragma once

#include "data.hpp"

namespace scuff::sbox::os {

[[nodiscard]] auto make_clap_window_ref(edwin::window* wnd) -> clap_window_t;

} // scuff::sbox::os