#include "common/os.hpp"
#include "common/shm.hpp"
#include "common/visit.hpp"
#include "cmdline.hpp"
#include "data.hpp"
#include "msg-proc.hpp"
#include <iostream>
#include <optional>
#include <string_view>

namespace scuff::sbox {

static
auto audio_thread_proc(std::stop_token stop_token, sbox::app* app) -> void {
	// TODO:
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
	app->shm            = shm::sandbox{bip::open_only, shmid.c_str()};
	app->audio_thread   = std::jthread{audio_thread_proc, app};
	app->main_thread_id = std::this_thread::get_id();
	scuff::os::set_realtime_priority(&app->audio_thread);
	return app;
}

static
auto destroy(sbox::app** app) -> void {
	if ((*app)->audio_thread.joinable()) {
		(*app)->audio_thread.request_stop();
		(*app)->audio_thread.join();
	}
	delete *app;
}

static
auto update(sbox::app* app, const real64_t prtime, const real64_t ctime) -> void {
	process_messages(app);
}

} // scuff::sbox

#include <osmain.h>
osmain_sync(0.1, scuff::sbox::create, scuff::sbox::destroy, scuff::sbox::update, "", scuff::sbox::app)
