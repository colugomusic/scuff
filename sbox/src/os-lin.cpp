#include "common-os.hpp"
#include "os.hpp"

namespace scuff::sbox::os {

auto make_clap_window_ref(edwin::window* wnd) -> clap_window_t {
	clap_window_t ref;
	ref.api = scuff::os::get_clap_window_api();
	ref.x11 = edwin::get_xwindow(*wnd);
	return ref;
}

auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	// Nothing to do.
}

auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	// Nothing to do.
}


} // scuff::sbox::os
