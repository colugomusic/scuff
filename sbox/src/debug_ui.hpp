#pragma once

#include <nappgui.h>

namespace scuff::sbox::debug_ui {

#if _DEBUG

struct model {
	Window* window     = nullptr;
	Panel* panel       = nullptr;
	Layout* layout     = nullptr;
	TextView* log_view = nullptr;
};

static
auto window_OnClose(model* m, Event* e) -> void {
	osapp_finish();
}

static
auto create(model* m) -> void {
	m->window = window_create(ekWINDOW_STDRES);
	m->layout = layout_create(1, 1);
	m->panel  = panel_create();
	m->log_view = textview_create();
	panel_layout(m->panel, m->layout);
	layout_textview(m->layout, m->log_view, 0, 0);
	window_panel(m->window, m->panel);
	window_size(m->window, s2df(800, 600));
	window_show(m->window);
	window_OnClose(m->window, listener(m, debug_ui::window_OnClose, debug_ui::model));
}

static
auto destroy(model* m) -> void {
	if (m->window) {
		window_destroy(&m->window);
	}
}

template <typename... Args> static
auto log(model* m, std::string_view msg, Args&&... args) -> void {
	textview_printf(m->log_view, msg.data(), std::forward<Args>(args)...);
}

#else

struct model {};
static auto create(model* m) -> void {}
static auto destroy(model* m) -> void {}
template <typename... Args> static auto log(model* m, std::string_view msg, Args&&... args) -> void {}

#endif

} // scuff::sbox::debug_ui