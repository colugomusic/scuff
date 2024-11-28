#include "common-os.hpp"
#include "os.hpp"

namespace scuff::sbox::os::gtk {

auto get_toplevel_widget(void* gtk_widget) -> void*;
auto get_window_from_widget(void* gtk_widget) -> void*;

} // scuff::sbox::os::gtk

namespace scuff::sbox::os {

auto get_editor_window_native_handle(const sbox::device& dev) -> void* {
	return gtk::get_toplevel_widget(dev.ui.view);
}

auto get_window_handle_for_clap(void* widget) -> void* {
	return gtk::get_window_from_widget(widget);
}

auto make_clap_window_ref(View* view) -> clap_window_t {
	clap_window_t ref;
	ref.api = scuff::os::get_clap_window_api();
	ref.x11 = get_window_handle_for_clap(view_native(view));
	return ref;
}

auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	// Nothing to do.
}

auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	// Nothing to do.
}

} // scuff::sbox::os
