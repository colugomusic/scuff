#pragma once

#include "clap.hpp"
#include "common-shm.hpp"
#include "data.hpp"
#include "log.hpp"

namespace scuff::sbox::audio {

static
auto copy_data_from_output(const shm::device& dest, size_t dest_port_index, const shm::device& source, size_t src_port_index) -> void {
	const auto& output_buffer = source.data->audio_out.at(src_port_index);
	auto& input_buffer        = dest.data->audio_in.at(dest_port_index);
	input_buffer = output_buffer;
}

static
auto copy_data_from_connected_outputs(const sbox::app& app, const sbox::device& dev) -> void {
	for (const auto& conn : dev.output_conns) {
		copy_data_from_output(app.audio_model->devices.at(conn.other_device).service->shm, conn.other_port_index, dev.service->shm, conn.this_port_index);
	}
}

static
auto transfer_input_events_from_main(const sbox::device& dev) -> void {
	scuff::event event;
	auto& events_in = dev.service->shm.data->events_in;
	while (dev.service->input_events_from_main.try_dequeue(event)) {
		if (events_in.size() == events_in.max_size()) {
			debug_log("Dropping input events because the input event queue is full. This is a bug!");
			break;
		}
		dev.service->shm.data->events_in.push_back(event);
	}
}

static
auto do_processing(const sbox::app& app, const sbox::device& dev) -> void {
	transfer_input_events_from_main(dev);
	switch (dev.type) {
		case plugin_type::clap: {
			scuff::sbox::clap::audio::process(app, dev);
			break;
		}
		case plugin_type::vst3: {
			// Not implemented yet.
			break;
		}
	}
	audio::copy_data_from_connected_outputs(app, dev);
}

static
auto do_processing(sbox::app* app) -> void {
	app->audio_model = app->model.read(ez::audio);
	for (const auto dev_id : app->audio_model->device_processing_order) {
		const auto dev = app->audio_model->devices.at(dev_id);
		do_processing(*app, dev);
	}
	if (!signaling::notify_sandbox_done(app->group_signaler)) {
		throw std::runtime_error("Failed to signal sandbox processing complete!");
	}
	app->audio_model = {};
}

static
auto thread_proc(std::stop_token stop_token, sbox::app* app) -> void {
	try {
		debug_log("Audio thread has started.");
		for (;;) {
			auto result = signaling::wait_for_work_begin(app->sandbox_signaler, stop_token);
			if (result == signaling::sandbox_wait_result::signaled) {
				do_processing(app);
				continue;
			}
			if (result == signaling::sandbox_wait_result::stop_requested) {
				debug_log("Audio thread is stopping because it was requested to.");
				return;
			}
			throw std::runtime_error("Unexpected sandbox_wait_result");
		}
	}
	catch (const std::exception& err) {
		debug_log("Audio thread is stopping because there was a fatal error: %s", err.what());
		app->msgs_out.lock()->push_back(msg::out::report_error{err.what()});
		app->schedule_terminate = true;
	}
}

static
auto start(sbox::app* app) -> void {
	debug_log("audio::start()");
	if (app->audio_thread.joinable()) {
		return;
	}
	app->audio_thread = std::jthread{audio::thread_proc, app};
	scuff::os::set_realtime_priority(&app->audio_thread);
}

static
auto stop(sbox::app* app) -> void {
	debug_log("audio::stop()");
	if (app->audio_thread.joinable()) {
		app->audio_thread.request_stop();
		app->audio_thread.join();
	}
}

} // scuff::sbox::audio



