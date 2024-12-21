#define DOCTEST_CONFIG_IMPLEMENT
#include "common-os.hpp"
#include "common-visit.hpp"
#include "cmdline.hpp"
#include "doctest.h"
#include "msg-proc.hpp"
#include <chrono>
#include <cmrc/cmrc.hpp>
#include <filesystem>
#include <fulog.hpp>
#include <iostream>
#include <optional>
#include <platform_folders.h>
#include <string_view>
#include <tga.h>

CMRC_DECLARE(scuff::sbox);

namespace fs = std::filesystem;

namespace scuff::sbox::main {

static sbox::app* app_ = nullptr;

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
		fu::log("ERROR: Heartbeat timeout");
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
	if (app.options.test)                  { return sbox::mode::test; }
	if (!app.options.gui_file.empty()) { return sbox::mode::gui_test; }
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
	fu::log(std::format("INFO: client PID: {}", app->options.client_pid));
	fu::log(std::format("INFO: group: {}", app->options.group_shmid));
	fu::log(std::format("INFO: sandbox: {}", app->options.sbox_shmid));
	app->shm_group              = shm::open_group(app->options.group_shmid);
	app->shm_sbox               = shm::open_sandbox(app->options.sbox_shmid);
	app->group_signaler.local   = &app->shm_group.signaling;
	app->group_signaler.shm     = &app->shm_group.data->signaling;
	app->sandbox_signaler.local = &app->shm_sbox.signaling;
	app->sandbox_signaler.shm   = &app->shm_sbox.data->signaling;
	app->main_thread_id         = std::this_thread::get_id();
	app->last_heartbeat         = std::chrono::steady_clock::now();
	auto frame = [app]{
		process_client_messages(ez::main, app);
		do_scheduled_window_resizes(app);
		edwin::process_messages();
		check_heartbeat(app);
		clap::update(ez::main, app);
		send_msgs_out(app);
		if (app->schedule_terminate) {
			edwin::app_end();
		}
	};
	fu::debug_log("INFO: Entering message loop...");
	edwin::app_beg({frame}, {std::chrono::milliseconds{50}});
	fu::debug_log("INFO: Cleanly exiting...");
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
	fu::log(std::format("INFO: gui_file: {}", app->options.gui_file));
	app->main_thread_id = std::this_thread::get_id();
	auto on_window_closed = [app]{
		app->schedule_terminate = true;
	};
	op::device_create(app, plugin_type::clap, id::device{1}, app->options.gui_file, app->options.gui_id);
	gui::show(ez::main, app, id::device{1}, {on_window_closed});
	auto frame = [app]{
		do_scheduled_window_resizes(app);
		edwin::process_messages();
		clap::update(ez::main, app);
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

static
auto run_tests(sbox::app* app, int pid) -> int {
	app->options.sbox_shmid = "scuff-sbox-test+" + std::to_string(pid);
	doctest::Context ctx;
	return ctx.run();
}

auto make_window_icon() -> sbox::icon {
	const auto fs = cmrc::scuff::sbox::get_filesystem();
	const auto clap_icon_256_tga = fs.open("res/clap-icon-256.tga");
	const auto bytes = std::span<const std::byte>{reinterpret_cast<const std::byte*>(clap_icon_256_tga.begin()), clap_icon_256_tga.size()};
	auto rofile  = tga::ReadOnlyMemoryFileInterface{bytes};
	auto decoder = tga::Decoder(&rofile);
	auto header  = tga::Header{};
	if (!decoder.readHeader(header)) {
		return {};
	}
	auto image = tga::Image{};
	image.bytesPerPixel = header.bytesPerPixel();
	image.rowstride     = header.width * image.bytesPerPixel;
	auto buffer = std::vector<uint8_t>(image.rowstride * header.height);
	image.pixels = buffer.data();
	if (!decoder.readImage(header, image)) {
		return {};
	}
	decoder.postProcessImage(header, image);
	sbox::icon icon;
	icon.size = edwin::size{header.width, header.height};
	for (auto y = 0; y < header.height; ++y) {
		for (auto x = 0; x < header.width; ++x) {
			const auto index = (y * header.width + x) * 4;
			const auto r = static_cast<std::byte>(image.pixels[index + 0]);
			const auto g = static_cast<std::byte>(image.pixels[index + 1]);
			const auto b = static_cast<std::byte>(image.pixels[index + 2]);
			const auto a = static_cast<std::byte>(image.pixels[index + 3]);
			icon.pixels.push_back(edwin::rgba{r, g, b, a});
		}
	}
	return icon;
}

auto go(int argc, const char* argv[]) -> int {
	const auto pid = scuff::os::get_process_id();
	sbox::app app;
	app_ = &app;
	fu::delete_old_files(std::chrono::hours{48});
	fu::set_application_name("scuff-sbox");
	try {
		fu::log("INFO: scuff-sbox started");
		app.options     = cmdline::get_options(app, argc, argv);
		app.mode        = get_mode(app);
		app.window_icon = make_window_icon();
		if (app.mode == sbox::mode::sandbox)  { return sandbox(&app); }
		if (app.mode == sbox::mode::gui_test) { return gui_test(&app); }
		if (app.mode == sbox::mode::test)     { return run_tests(&app, pid); }
	}
	catch (const std::exception& err) {
		fu::log(std::format("ERROR: {}", err.what()));
	}
	catch (...) {
		fu::log("ERROR: Unknown error");
	}
	return EXIT_FAILURE;
}

TEST_CASE("com.FabFilter.preset-discovery.Saturn.2") {
	const auto plugfile_path = "C:\\Program Files\\Common Files\\CLAP\\FabFilter Saturn 2.clap";
	op::device_create(app_, plugin_type::clap, id::device{1}, plugfile_path, "com.FabFilter.preset-discovery.Saturn.2");
}

} // scuff::sbox::main

namespace boost::interprocess::ipcdetail {

auto get_shared_dir(std::string& shared_dir) -> void  {
	const auto pid = scuff::sbox::main::app_->options.client_pid;
	shared_dir = scuff::shm::get_shm_emulation_process_dir(sago::getDataHome(), pid).string();
} 

auto get_shared_dir(std::wstring& shared_dir) -> void {
	const auto pid = scuff::sbox::main::app_->options.client_pid;
	shared_dir = scuff::shm::get_shm_emulation_process_dir(sago::getDataHome(), pid).wstring();
} 

} // namespace boost::interprocess::ipcdetail

auto main(int argc, const char* argv[]) -> int {
	return scuff::sbox::main::go(argc, argv);
}
