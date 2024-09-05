#include "common/visit.hpp"
#include "app.hpp"
#include "cmdline.hpp"
#include "device.hpp"
#include "plugfile.hpp"
#include "shm.hpp"
#include <iostream>
#include <optional>
#include <string_view>

namespace sbox {

[[nodiscard]] static
auto create_panel(View* view) -> Panel* {
	const auto panel = panel_create();
	const auto layout = layout_create(1, 1);
	layout_view(layout, view, 0, 0);
	panel_layout(panel, layout);
	return panel;
}

[[nodiscard]] static
auto create_window(Panel* panel) -> Window* {
	const auto window = window_create(ekWINDOW_STD | ekWINDOW_RESIZE);
	window_panel(window, panel);
	window_title(window, "tom");
	return window;
}

static
auto on_window_close(sbox::app* app, Event* e) -> void {
	const auto p     = event_params(e, EvWinClose);
	const auto close = event_result(e, bool_t);
	cassert_no_null(app);
	switch (p->origin) {
		case ekGUI_CLOSE_BUTTON: {
			// TODO:
			osapp_finish();
			break;
		}
		case ekGUI_CLOSE_DEACT: {
			cassert_default();
		}
	}
}

[[nodiscard]] static
auto create() -> sbox::app* {
	device::create();
	plugfile::create();
	const auto app = heap_new0(sbox::app);
	app->options  = cmdline::get_options();
	if (app->options.group.empty()) {
		log_printf("Missing required option --group");
		osapp_finish();
		return app;
	}
	if (app->options.sandbox.empty()) {
		log_printf("Missing required option --sandbox");
		osapp_finish();
		return app;
	}
	if (!shm::open(app->options.group, app->options.sandbox)) {
		osapp_finish();
		return app;
	}
	//app->view = view_create();
	//app->panel = create_panel(app->view);
	//app->window = create_window(app->panel);
	//window_origin(app->window, v2df(500, 200));
	//window_OnClose(app->window, listener(app, on_window_close, tom));
	//window_show(app->window);
	return app;
}

static
auto destroy(sbox::app** app) -> void {
	if ((*app)->window) {
		window_destroy(&(*app)->window);
	}
	device::destroy();
	plugfile::destroy();
	shm::destroy();
	heap_delete(app, sbox::app);
}

static
auto process_input_msg_(const scuff::msg::in::close_all_editors& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::commit_changes& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::device_add& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::device_connect& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::device_disconnect& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::device_erase& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::device_gui_hide& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::device_gui_show& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::device_set_render_mode& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::event& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::find_param& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::get_param_value& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::get_param_value_text& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(const scuff::msg::in::set_sample_rate& msg) -> void {
	// TODO:
}

static
auto process_input_msg(const scuff::msg::in::msg& msg) -> void {
	fast_visit([](const auto& msg) { process_input_msg_(msg); }, msg);
}

static
auto update(sbox::app* app, const real64_t prtime, const real64_t ctime) -> void {
	static std::vector<scuff::msg::in::msg> input_msgs;
	input_msgs.clear();
	shm::receive_input_messages(&input_msgs);
	for (const auto& msg : input_msgs) {
		process_input_msg(msg);
	}
}

} // sbox

static constexpr auto UPDATE_INTERVAL = 0.1;

#include <osmain.h>
osmain_sync(UPDATE_INTERVAL, sbox::create, sbox::destroy, sbox::update, "", sbox::app)
