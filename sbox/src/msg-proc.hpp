#pragma once

#include "data.hpp"

namespace scuff::sbox {

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::close_all_editors& msg) -> void {
	const auto devices = app->working_model.lock()->devices;
	for (const auto& dev : devices) {
		if (dev.ui.window) {
			window_hide(dev.ui.window);
		}
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_create& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_connect& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_disconnect& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_erase& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_hide& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_show& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_load& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_save& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_set_render_mode& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::event& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::find_param& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::get_param_value& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::get_param_value_text& msg) -> void {
	// TODO:
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::set_sample_rate& msg) -> void {
	// TODO:
}

static
auto process_input_msg(sbox::app* app, const scuff::msg::in::msg& msg) -> void {
	fast_visit([app](const auto& msg) { process_input_msg_(app, msg); }, msg);
}

static
auto process_messages(sbox::app* app) -> void {
	const auto receive = [app](std::byte* bytes, size_t count) -> size_t {
		return app->shm.receive_bytes(bytes, count);
	};
	const auto input_msgs = app->msg_receiver.receive(receive);
	for (const auto& msg : input_msgs) {
		process_input_msg(app, msg);
	}
}

} // scuff::sbox