#include <boost/container/stable_vector.hpp> // included before nappgui to avoid preprocessor conflicts
#include "common/os.hpp"
#include "common/visit.hpp"
#include "cmdline.hpp"
#include "log.hpp"
#include "msg-proc.hpp"
#include <iostream>
#include <optional>
#include <string_view>

namespace scuff::sbox::main {

[[nodiscard]] static
auto create() -> sbox::app* {
	const auto app = new sbox::app;
	app->options = cmdline::get_options();
	if (app->options.group_shmid.empty()) {
		log(app, "Missing required option --group");
		osapp_finish();
		return app;
	}
	if (app->options.sbox_shmid.empty()) {
		log(app, "Missing required option --sandbox");
		osapp_finish();
		return app;
	}
	if (app->options.sample_rate < 1) {
		log(app, "Missing required option --sr");
		osapp_finish();
		return app;
	}
	try {
		//while (!IsDebuggerPresent()) {
		//	std::this_thread::sleep_for(std::chrono::milliseconds(100));
		//}
		//__debugbreak();
		debug_ui::create(&app->debug_ui);
		log(app, "group: %s", app->options.group_shmid.c_str());
		log(app, "sandbox: %s", app->options.sbox_shmid.c_str());
		log(app, "sample rate: %f", app->options.sample_rate);
		app->shm_group      = shm::group{bip::open_only, app->options.group_shmid};
		app->shm_sbox       = shm::sandbox{bip::open_only, app->options.sbox_shmid};
		app->main_thread_id = std::this_thread::get_id();
	}
	catch (const std::exception& err) {
		log(app, "Error: %s", err.what());
		osapp_finish();
	}
	return app;
}

static
auto destroy(sbox::app** app) -> void {
	debug_ui::destroy(&(*app)->debug_ui);
	if ((*app)->audio_thread.joinable()) {
		(*app)->audio_thread.request_stop();
		(*app)->audio_thread.join();
	}
	delete *app;
}

static
auto update(sbox::app* app, const real64_t prtime, const real64_t ctime) -> void {
	if (app) {
		main::process_messages(app);
		clap::main::update(app);
		if (app->schedule_terminate) {
			osapp_finish();
		}
	}
}

} // scuff::sbox::main

#include <osmain.h>
osmain_sync(0.1, scuff::sbox::main::create, scuff::sbox::main::destroy, scuff::sbox::main::update, "", scuff::sbox::app)
