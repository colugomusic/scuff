#include "common-os.hpp"
#include "os.hpp"
#include <Cocoa/Cocoa.h>

namespace scuff::sbox::os {

auto get_editor_window_native_handle(const sbox::device& dev) -> void* {
	const auto nsview = (NSView*)(view_native(dev.ui.view));
	return nsview.window;
}

auto make_clap_window_ref(View* view) -> clap_window_t {
	clap_window_t ref;
	ref.api   = scuff::os::get_clap_window_api();
	ref.cocoa = view_native(view);
	return ref;
}

auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	// Nothing to do.
}

auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	// Nothing to do.
}

} // scuff::sbox::os
