#pragma once

#include "clap.hpp"

namespace scuff::sbox {

[[nodiscard]] static
auto get_device_type(const sbox::app& app, id::device dev_id) -> scuff_plugin_type {
	return app.working_model.lock()->devices.at(dev_id).type;
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::clean_shutdown& msg) -> void {
	// TODO:
}

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
	const auto m = app->working_model.lock();
	sbox::device dev;
	dev.id   = {msg.dev_id};
	dev.type = msg.type;
	if (msg.type == scuff_plugin_type::clap) {
		sbox::clap::device clap_dev;
		// TODO:
		m->clap_devices = m->clap_devices.insert(clap_dev);
	}
	else {
		// Not implemented yet
	}
	m->devices = m->devices.insert(dev);
	app->published_model.set(*m);
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
	const auto m = app->working_model.lock();
	// TODO:
	m->devices      = m->devices.erase({msg.dev_id});
	m->clap_devices = m->clap_devices.erase({msg.dev_id});
	app->published_model.set(*m);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_hide& msg) -> void {
	const auto devices = app->working_model.lock()->devices;
	const auto device  = devices.at({msg.dev_id});
	window_hide(device.ui.window);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_show& msg) -> void {
	const auto devices = app->working_model.lock()->devices;
	const auto device  = devices.at({msg.dev_id});
	window_show(device.ui.window);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_load& msg) -> void {
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == scuff_plugin_type::clap) {
		if (!clap::load(app, dev_id, msg.state)) {
			app->msg_sender.enqueue(scuff::msg::out::report_error{"Failed to load device state"});
		}
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_save& msg) -> void {
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == scuff_plugin_type::clap) {
		const auto state = clap::save(app, dev_id);
		if (state.empty()) {
			app->msg_sender.enqueue(scuff::msg::out::report_error{"Failed to save device state"});
			return;
		}
		app->msg_sender.enqueue(scuff::msg::out::return_state{msg.dev_id, state, msg.callback});
		return;
	}
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
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == scuff_plugin_type::clap) {
		const auto text = clap::get_param_value_text(*app, dev_id, msg.param_idx, msg.value);
		app->msg_sender.enqueue(scuff::msg::out::return_param_value_text{text, msg.callback});
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::set_sample_rate& msg) -> void {
	// TODO: deactivate all devices and re-activate with the new sample rate
}

static
auto process_input_msg(sbox::app* app, const scuff::msg::in::msg& msg) -> void {
	fast_visit([app](const auto& msg) { process_input_msg_(app, msg); }, msg);
}

static
auto process_messages(sbox::app* app) -> void {
	try {
		const auto receive = [app](std::byte* bytes, size_t count) -> size_t {
			return app->shm.receive_bytes(bytes, count);
		};
		const auto send = [app](const std::byte* bytes, size_t count) -> size_t {
			return app->shm.send_bytes(bytes, count);
		};
		const auto input_msgs = app->msg_receiver.receive(receive);
		for (const auto& msg : input_msgs) {
			process_input_msg(app, msg);
		}
		app->msg_sender.send(send);
	}
	catch (const std::exception& err) {
		app->msg_sender.enqueue(scuff::msg::out::report_error{err.what()});
	}
}

} // scuff::sbox