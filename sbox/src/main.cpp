#include <boost/container/stable_vector.hpp> // included before nappgui to avoid preprocessor conflicts
#include "common-os.hpp"
#include "common-visit.hpp"
#include "cmdline.hpp"
#include "log.hpp"
#include "msg-proc.hpp"
#include <iostream>
#include <optional>
#include <string_view>

namespace scuff::sbox::main {

static app app_;

static
auto destroy_all_editor_windows(const sbox::app& app) -> void {
	const auto m = app.model.read(ez::main);
	for (auto dev : m.devices) {
		if (dev.ui.window) {
			ezwin::destroy(dev.ui.window);
		}
	}
}

static
auto check_heartbeat(sbox::app* app) -> void {
	const auto now  = std::chrono::steady_clock::now();
	const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - app->last_heartbeat).count();
	if (diff > HEARTBEAT_TIMEOUT_MS) {
		log(app, "Heartbeat timeout");
		app->msg_sender.enqueue(scuff::msg::out::report_error{"Heartbeat timeout"});
		app->schedule_terminate = true;
	}
}

static
auto do_scheduled_window_resizes(sbox::app* app) -> void {
	const auto m = app->model.read(ez::main);
	for (const auto& dev : m.devices) {
		if (dev.service->scheduled_window_resize) {
			ezwin::size sz;
			sz.width  = static_cast<int>(dev.service->scheduled_window_resize->width);
			sz.height = static_cast<int>(dev.service->scheduled_window_resize->height);
			ezwin::set(dev.ui.window, sz);
			dev.service->scheduled_window_resize.reset();
		}
	}
}

auto go(int argc, const char* argv[]) -> int {
	app_.options = cmdline::get_options(argc, argv);
	if (app_.options.group_shmid.empty()) {
		log(&app_, "Missing required option --group");
		return EXIT_FAILURE;
	}
	if (app_.options.sbox_shmid.empty()) {
		log(&app_, "Missing required option --sandbox");
		return EXIT_FAILURE;
	}
	try {
		log(&app_, "group: %s", app_.options.group_shmid.c_str());
		log(&app_, "sandbox: %s", app_.options.sbox_shmid.c_str());
		app_.shm_group              = shm::open_group(app_.options.group_shmid);
		app_.shm_sbox               = shm::open_sandbox(app_.options.sbox_shmid);
		app_.group_signaler.local   = &app_.shm_group.signaling;
		app_.group_signaler.shm     = &app_.shm_group.data->signaling;
		app_.sandbox_signaler.local = &app_.shm_sbox.signaling;
		app_.sandbox_signaler.shm   = &app_.shm_sbox.data->signaling;
		app_.main_thread_id         = std::this_thread::get_id();
		app_.last_heartbeat         = std::chrono::steady_clock::now();
		for (;;) {
			main::process_messages(&app_);
			do_scheduled_window_resizes(&app_);
			ezwin::process_messages();
			check_heartbeat(&app_);
			clap::main::update(&app_);
			if (app_.schedule_terminate) {
				break;
			}
		}
		if (app_.audio_thread.joinable()) {
			app_.audio_thread.request_stop();
			signaling::unblock_self(app_.sandbox_signaler);
			app_.audio_thread.join();
		}
		destroy_all_editor_windows(app_);
		ezwin::process_messages();
		return EXIT_SUCCESS;
	}
	catch (const std::exception& err) {
		log(&app_, "Error: %s", err.what());
		return EXIT_FAILURE;
	}
}

} // scuff::sbox::main

auto main(int argc, const char* argv[]) -> int {
	return scuff::sbox::main::go(argc, argv);
}
