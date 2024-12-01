#include "common-os.hpp"
#include "os.hpp"
#include <edwin-ext.hpp>
#include <Windows.h>

namespace scuff::sbox::os {

auto make_clap_window_ref(edwin::window* wnd) -> clap_window_t {
	clap_window_t ref;
	ref.api   = scuff::os::get_clap_window_api();
	ref.win32 = edwin::get_hwnd(*wnd);
	return ref;
}

} // scuff::sbox::os
