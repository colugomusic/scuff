#include "common/os.hpp"
#include "common/shm.hpp"
#include "common/visit.hpp"
#include "cmdline.hpp"
#include "data.hpp"
#include "msg-proc.hpp"
#include <iostream>
#include <optional>
#include <string_view>

namespace scuff::sbox::audio {

static
auto copy_data_to_connected_inputs(const sbox::model& m, const sbox::device& dev) -> void {
	for (const auto& conn : dev.input_conns) {
		// TODO:
	}
}

static
auto copy_data_to_connected_outputs(const sbox::model& m, const sbox::device& dev) -> void {
	for (const auto& conn : dev.output_conns) {
		// TODO:
	}
}

static
auto do_processing(const sbox::model& m, const sbox::device& dev) -> void {
	audio::copy_data_to_connected_inputs(m, dev);
	if (dev.type == scuff_plugin_type::clap) {
		scuff::sbox::clap::audio::process(m, dev);
	}
	else {
		// Not implemented yet.
	}
	audio::copy_data_to_connected_outputs(m, dev);
}

static
auto do_processing(sbox::app* app) -> void {
	const auto m = app->model.lockfree_read();
	for (const auto dev_id : m->device_processing_order) {
		const auto dev = m->devices.at(dev_id);
		do_processing(*m, dev);
	}
	const auto shm_group  = app->shm_group.data;
	const auto prev_value = shm_group->sandboxes_processing.fetch_sub(1, std::memory_order_release);
	if (prev_value == 1) {
		// Notify the client that all sandboxes have finished their work.
		shm_group->cv.notify_one();
	}
}

static
auto thread_proc(std::stop_token stop_token, sbox::app* app) -> void {
	try {
		uint64_t local_epoch = 0;
		uint64_t shm_epoch   = 0;
		const auto shm_group = app->shm_group.data;
		auto wait_condition = [shm_group, &local_epoch, &shm_epoch, &stop_token]{
			shm_epoch = shm_group->epoch;
			return shm_epoch > local_epoch || stop_token.stop_requested();
		};
		for (;;) {
			auto lock = std::unique_lock{shm_group->mut};
			shm_group->cv.wait(lock, wait_condition);
			if (shm_epoch > local_epoch) {
				do_processing(app);
				local_epoch = shm_epoch;
			}
			if (stop_token.stop_requested()) {
				return;
			}
		}
	}
	catch (const std::exception& err) {
		app->msg_sender.enqueue(msg::out::report_fatal_error{err.what()});
		app->schedule_terminate = true;
	}
}

} // scuff::sbox::audio

namespace scuff::sbox::main {

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
	const auto shmid_sbox  = shm::sandbox::make_id(app->options.instance_id, app->options.sbox_id);
	const auto shmid_group = shm::group::make_id(app->options.instance_id, app->options.group_id);
	app->shm_group      = shm::group{bip::open_only, shmid_group.c_str()};
	app->shm_sbox       = shm::sandbox{bip::open_only, shmid_sbox.c_str()};
	app->audio_thread   = std::jthread{audio::thread_proc, app};
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
	main::process_messages(app);
	clap::main::update(app);
	if (app->schedule_terminate) {
		osapp_finish();
	}
}

} // scuff::sbox::main

#include <osmain.h>
osmain_sync(0.1, scuff::sbox::main::create, scuff::sbox::main::destroy, scuff::sbox::main::update, "", scuff::sbox::app)
