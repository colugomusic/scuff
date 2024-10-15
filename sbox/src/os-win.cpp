#include "os.hpp"
#include <Windows.h>

namespace scuff::sbox::os {

[[nodiscard]] static
auto get_top_hwnd(HWND hwnd) -> HWND {
	return GetAncestor(hwnd, GA_ROOT);
}

auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	const auto view_hwnd = (HWND)(view_native(dev.ui.view));
	const auto top_hwnd  = GetAncestor(view_hwnd, GA_ROOT);
	SetWindowPos(top_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	// Nothing to do.
}

} // scuff::sbox::os
