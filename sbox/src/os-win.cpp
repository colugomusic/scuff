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

auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	const auto view_hwnd = static_cast<HWND>(edwin::get_native_handle(*dev.ui.window).value);
	const auto top_hwnd  = GetAncestor(view_hwnd, GA_ROOT);
	SetWindowPos(top_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	// Nothing to do.
}

} // scuff::sbox::os
