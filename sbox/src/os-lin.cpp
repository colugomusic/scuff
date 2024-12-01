#include "common-os.hpp"
#include "os.hpp"

namespace scuff::sbox::os {

auto make_clap_window_ref(edwin::window* wnd) -> clap_window_t {
	clap_window_t ref;
	ref.api = scuff::os::get_clap_window_api();
	ref.x11 = edwin::get_xwindow(*wnd);
	return ref;
}

} // scuff::sbox::os
