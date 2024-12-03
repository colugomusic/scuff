#include <boost/container/stable_vector.hpp> // included before nappgui to avoid preprocessor conflicts
#include "common-os.hpp"
#include "common-visit.hpp"
#include "cmdline.hpp"
#include "loguru.hpp"
#include "msg-proc.hpp"
#include <filesystem>
#include <iostream>
#include <optional>
#include <platform_folders.h>
#include <string_view>

namespace fs = std::filesystem;

namespace scuff::sbox::main {

static
auto destroy_all_editor_windows(const sbox::app& app) -> void {
	const auto m = app.model.read(ez::main);
	for (auto dev : m.devices) {
		if (dev.ui.window) {
			edwin::destroy(dev.ui.window);
		}
	}
}

static
auto check_heartbeat(sbox::app* app) -> void {
	const auto now  = std::chrono::steady_clock::now();
	const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - app->last_heartbeat).count();
	if (diff > HEARTBEAT_TIMEOUT_MS) {
		LOG_S(ERROR) << "Heartbeat timeout";
		app->msgs_out.lock()->push_back(scuff::msg::out::report_error{"Heartbeat timeout"});
		app->schedule_terminate = true;
	}
}

static
auto do_scheduled_window_resizes(sbox::app* app) -> void {
	const auto m = app->model.read(ez::main);
	for (const auto& dev : m.devices) {
		if (dev.service->scheduled_window_resize) {
			edwin::size sz;
			sz.width  = static_cast<int>(dev.service->scheduled_window_resize->width);
			sz.height = static_cast<int>(dev.service->scheduled_window_resize->height);
			edwin::set(dev.ui.window, sz);
			dev.service->scheduled_window_resize.reset();
		}
	}
}

static
auto get_mode(const sbox::app& app) -> sbox::mode {
	if (!app.options.plugfile_gui.empty()) {
		return sbox::mode::gui_test;
	}
	return sbox::mode::sandbox;
}

static
auto send_msgs_out(sbox::app* app) -> void {
	if (app->mode == sbox::mode::sandbox) {
		const auto msgs_out = app->msgs_out.lock();
		for (const auto& msg : *msgs_out) {
			app->client_msg_sender.enqueue(msg);
		}
		msgs_out->clear();
	}
	else {
		const auto msgs_out = app->msgs_out.lock();
		msgs_out->clear();
	}
}

auto sandbox(sbox::app* app) -> int {
	LOG_S(INFO) << "group: " << app->options.group_shmid;
	LOG_S(INFO) << "sandbox: " << app->options.sbox_shmid;
	app->shm_group              = shm::open_group(app->options.group_shmid);
	app->shm_sbox               = shm::open_sandbox(app->options.sbox_shmid);
	app->group_signaler.local   = &app->shm_group.signaling;
	app->group_signaler.shm     = &app->shm_group.data->signaling;
	app->sandbox_signaler.local = &app->shm_sbox.signaling;
	app->sandbox_signaler.shm   = &app->shm_sbox.data->signaling;
	app->main_thread_id         = std::this_thread::get_id();
	app->last_heartbeat         = std::chrono::steady_clock::now();
	auto frame = [app]{
		main::process_client_messages(app);
		do_scheduled_window_resizes(app);
		edwin::process_messages();
		check_heartbeat(app);
		clap::main::update(app);
		send_msgs_out(app);
		if (app->schedule_terminate) {
			edwin::app_end();
		}
	};
	DLOG_S(INFO) << "Entering message loop...";
	edwin::app_beg({frame}, {std::chrono::milliseconds{50}});
	DLOG_S(INFO) << "Cleanly exiting...";
	if (app->audio_thread.joinable()) {
		app->audio_thread.request_stop();
		signaling::unblock_self(app->sandbox_signaler);
		app->audio_thread.join();
	}
	destroy_all_editor_windows(*app);
	edwin::process_messages();
	return EXIT_SUCCESS;
}

static
auto gui_test(sbox::app* app) -> int {
	LOG_S(INFO) << "plugfile_gui: " << app->options.plugfile_gui;
	app->main_thread_id = std::this_thread::get_id();
	auto on_window_closed = [app]{
		app->schedule_terminate = true;
	};
	op::device_create(app, plugin_type::clap, id::device{1}, app->options.plugfile_gui, "ANY");
	gui::show(app, id::device{1}, {on_window_closed});
	auto frame = [app]{
		do_scheduled_window_resizes(app);
		edwin::process_messages();
		clap::main::update(app);
		send_msgs_out(app);
		if (app->schedule_terminate) {
			edwin::app_end();
		}
	};
	edwin::app_beg({frame}, {std::chrono::milliseconds{50}});
	destroy_all_editor_windows(*app);
	edwin::process_messages();
	return EXIT_SUCCESS;
}

auto get_log_file_path() -> fs::path {
	return fs::path(sago::getDataHome()) / "scuff-sbox" / "log.txt";
}

auto go(int argc, const char* argv[]) -> int {
	sbox::app app;
	loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
	loguru::init(argc, const_cast<char**>(argv));
	loguru::add_file(get_log_file_path().string().c_str(), loguru::Truncate, loguru::Verbosity_MAX);
	try {
		LOG_S(INFO) << "scuff-sbox started";
		app.options = cmdline::get_options(app, argc, argv);
		app.mode    = get_mode(app);
		if (app.mode == sbox::mode::sandbox)  { return sandbox(&app); }
		if (app.mode == sbox::mode::gui_test) { return gui_test(&app); }
	}
	catch (const std::exception& err) {
		LOG_S(ERROR) << err.what();
	}
	catch (...) {
		LOG_S(ERROR) << "Unknown error";
	}
	return EXIT_FAILURE;
}

} // scuff::sbox::main

auto main(int argc, const char* argv[]) -> int {
	return scuff::sbox::main::go(argc, argv);
}
