#include "common-os.hpp"
#include "edwin-ext.hpp"
#include "os.hpp"

namespace scuff::sbox::os {

auto make_clap_window_ref(edwin::window* wnd) -> clap_window_t {
    clap_window_t ref;
    ref.api   = scuff::os::get_clap_window_api();
    ref.cocoa = edwin::get_nsview(*wnd);
    return ref;
}

} // scuff::sbox::os
