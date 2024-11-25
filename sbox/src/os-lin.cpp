#include "common-os.hpp"
#include "os.hpp"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

namespace scuff::sbox::os {

auto get_editor_window_native_handle(const sbox::device& dev) -> void* {
	const auto widget = (GtkWidget*)(dev.ui.view);
	return gtk_widget_get_toplevel(widget);
}

auto get_window_handle_for_clap(void* widget) -> void* {
	const auto widget = static_cast<GtkWidget*>(widget);
	if (!gtk_widget_get_window(widget)) {
		gtk_widget_realize(widget);
	}
	if (const auto window = gtk_widget_get_window(widget); GDK_IS_X11_WINDOW(window)) {
		return (void*)(gdk_x11_window_get_xid(window));
	}
	return nullptr;
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
