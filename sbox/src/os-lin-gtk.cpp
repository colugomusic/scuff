#include <gtk/gtk.h>
#include <gdk/gdkx.h>

namespace scuff::sbox::os::gtk {

auto get_toplevel_widget(void* ptr) -> void* {
	const auto widget = static_cast<GtkWidget*>(ptr);
	return gtk_widget_get_toplevel(widget);
}

auto get_window_from_widget(void* ptr) -> void* {
	auto widget = static_cast<GtkWidget*>(ptr);
	auto top = static_cast<GtkWidget*>(get_toplevel_widget(ptr));
	if (!gtk_widget_get_window(top)) {
		gtk_widget_realize(top);
	}
	if (const auto window = gtk_widget_get_window(top); GDK_IS_X11_WINDOW(top)) {
		gtk_widget_hide(widget);
		return (void*)(gdk_x11_window_get_xid(window));
	}
	return nullptr;
}

} // scuff::sbox::os::gtk
