#include <gtk/gtk.h>
#include <gdk/gdkx.h>

namespace scuff::sbox::os::gtk {

auto get_toplevel_widget(void* ptr) -> void* {
	const auto widget = static_cast<GtkWidget*>(ptr);
	return gtk_widget_get_toplevel(widget);
}

auto get_window_from_widget(void* ptr) -> void* {
	const auto widget = static_cast<GtkWidget*>(ptr);
	if (!gtk_widget_get_window(widget)) {
		gtk_widget_realize(widget);
	}
	if (const auto window = gtk_widget_get_window(widget); GDK_IS_X11_WINDOW(window)) {
		return (void*)(gdk_x11_window_get_xid(window));
	}
	return nullptr;
}

} // scuff::sbox::os::gtk
