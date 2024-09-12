#include "common/shm.hpp"
#include "common/visit.hpp"
#include "cmdline.hpp"
#include "data.hpp"
#include "msg-proc.hpp"
#include <iostream>
#include <optional>
#include <string_view>

namespace scuff::sbox {

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
	const auto app = new sbox::app;
	app->options = cmdline::get_options();
	if (app->options.instance_id.empty()) {
		log_printf("Missing required option --instance-id");
		osapp_finish();
		return app;
	}
	if (!app->options.group_id) {
		log_printf("Missing required option --group");
		osapp_finish();
		return app;
	}
	if (!app->options.sbox_id) {
		log_printf("Missing required option --sandbox");
		osapp_finish();
		return app;
	}
	const auto shmid = shm::sandbox::make_id(app->options.instance_id, app->options.sbox_id);
	app->shm = shm::sandbox{bip::open_only, shmid.c_str()};
	return app;
}

static
auto destroy(sbox::app** app) -> void {
	delete *app;
}

static
auto update(sbox::app* app, const real64_t prtime, const real64_t ctime) -> void {
	process_messages(app);
}

} // scuff::sbox

#include <osmain.h>
osmain_sync(0.1, scuff::sbox::create, scuff::sbox::destroy, scuff::sbox::update, "", scuff::sbox::app)
