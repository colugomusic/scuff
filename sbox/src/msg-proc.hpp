#pragma once

#include "clap.hpp"
#include <format>
#include <immer/vector_transient.hpp>

namespace scuff::sbox::main {

[[nodiscard]] static
auto get_device_type(const sbox::app& app, id::device dev_id) -> scuff_plugin_type {
	return app.model.lock_read().devices.at(dev_id).type;
}

[[nodiscard]] static
auto is_a_connected_to_input_of_b(const device& a, const device& b) -> bool {
	for (const auto& conn : b.output_conns) {
		if (conn.other_device == a.id) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] static
auto find_insertion_index(const std::vector<device>& order, const device& dev) -> size_t {
	if (order.empty()) {
		return 0;
	}
	for (size_t i = 0; i < order.size(); ++i) {
		const auto& b = order[i];
		if (is_a_connected_to_input_of_b(dev, b)) {
			return i;
		}
	}
	return order.size();
}

[[nodiscard]] static
auto make_device_processing_order(immer::table<device> devices) -> immer::vector<id::device> {
	std::vector<device> order;
	for (const auto& dev : devices) {
		const auto index = find_insertion_index(order, dev);
		order.insert(order.begin() + index, dev);
	}
	immer::vector_transient<id::device> t;
	for (const auto& dev : order) {
		t.push_back(dev.id);
	}
	return t.persistent();
}

static
auto set_sample_rate(sbox::app* app, const sbox::device& dev, double sr) -> void {
	if (dev.type == scuff_plugin_type_clap) {
		if (!clap::main::set_sample_rate(*app, dev.id, sr)) {
			app->msg_sender.enqueue(scuff::msg::out::report_error{std::format("Failed to set sample rate for device {}", dev.id.value)});
		}
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::clean_shutdown& msg) -> void {
	// TODO: msg::in::clean_shutdown
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::close_all_editors& msg) -> void {
	const auto devices = app->model.lock_read().devices;
	for (const auto& dev : devices) {
		if (dev.ui.window) {
			window_hide(dev.ui.window);
		}
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_create& msg) -> void {
	try {
		if (msg.type == scuff_plugin_type_clap) {
			clap::main::create_device(app, {msg.dev_id}, msg.plugfile_path, msg.plugin_id, msg.callback);
			auto m = app->model.lock_read();
			m.device_processing_order = make_device_processing_order(m.devices);
			app->model.lock_write(m);
			app->model.lock_publish(m);
			return;
		}
		else {
			// Not implemented yet
			throw std::runtime_error("Unsupported device type");
		}
	}
	catch (const std::exception& err) {
		throw std::runtime_error(std::format("Failed to create device: {}", err.what()));
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_connect& msg) -> void {
	auto m           = app->model.lock_read();
	auto in_dev_ptr  = m.devices.find({msg.in_dev_id});
	auto out_dev_ptr = m.devices.find({msg.out_dev_id});
	if (!in_dev_ptr)  { throw std::runtime_error(std::format("Input device {} doesn't exist in this sandbox!", msg.in_dev_id)); }
	if (!out_dev_ptr) { throw std::runtime_error(std::format("Output device {} doesn't exist in this sandbox!", msg.out_dev_id)); }
	auto out_dev = *out_dev_ptr;
	port_conn conn;
	conn.other_device     = {msg.in_dev_id};
	conn.other_port_index = msg.in_port;
	conn.this_port_index  = msg.out_port;
	out_dev.output_conns = out_dev.output_conns.push_back(conn);
	m.devices                 = m.devices.insert(out_dev);
	m.device_processing_order = make_device_processing_order(m.devices);
	app->model.lock_write(m);
	app->model.lock_publish(m);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_disconnect& msg) -> void {
	auto m                 = app->model.lock_read();
	const auto in_dev_ptr  = m.devices.find({msg.in_dev_id});
	const auto out_dev_ptr = m.devices.find({msg.out_dev_id});
	if (!in_dev_ptr)  { throw std::runtime_error(std::format("Input device {} doesn't exist in this sandbox!", msg.in_dev_id)); }
	if (!out_dev_ptr) { throw std::runtime_error(std::format("Output device {} doesn't exist in this sandbox!", msg.out_dev_id)); }
	auto out_dev = *out_dev_ptr;
	port_conn conn;
	conn.other_device     = {msg.in_dev_id};
	conn.other_port_index = msg.in_port;
	conn.this_port_index  = msg.out_port;
	auto pos = std::find(out_dev.output_conns.begin(), out_dev.output_conns.end(), conn);
	if (pos == out_dev.output_conns.end()) {
		throw std::runtime_error(std::format("Output device {} port {} is not connected to input device {} port {}!", msg.out_dev_id, msg.out_port, msg.in_dev_id, msg.in_port));
	}
	out_dev.output_conns      = out_dev.output_conns.erase(pos.index());
	m.devices                 = m.devices.insert(out_dev);
	m.device_processing_order = make_device_processing_order(m.devices);
	app->model.lock_write(m);
	app->model.lock_publish(m);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_erase& msg) -> void {
	auto m             = app->model.lock_read();
	const auto dev_id  = id::device{msg.dev_id};
	const auto devices = m.devices;
	// Remove any internal connections to this device
	for (auto dev : devices) {
		for (auto pos = dev.output_conns.begin(); pos != dev.output_conns.end(); ++pos) {
			if (pos->other_device == dev_id) {
				dev.output_conns = dev.output_conns.erase(pos.index());
			}
		}
		m.devices = m.devices.insert(dev);
	}
	m.devices                 = m.devices.erase({msg.dev_id});
	m.clap_devices            = m.clap_devices.erase({msg.dev_id});
	m.device_processing_order = make_device_processing_order(m.devices);
	app->model.lock_write(m);
	app->model.lock_publish(m);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_hide& msg) -> void {
	const auto devices = app->model.lock_read().devices;
	const auto device  = devices.at({msg.dev_id});
	window_hide(device.ui.window);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_show& msg) -> void {
	const auto devices = app->model.lock_read().devices;
	const auto device  = devices.at({msg.dev_id});
	window_show(device.ui.window);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_load& msg) -> void {
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == scuff_plugin_type_clap) {
		if (!clap::main::load(app, dev_id, msg.state)) {
			app->msg_sender.enqueue(scuff::msg::out::report_error{"Failed to load device state"});
		}
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_save& msg) -> void {
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == scuff_plugin_type_clap) {
		const auto state = clap::main::save(app, dev_id);
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
	// TODO: msg::in::device_set_render_mode
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::event& msg) -> void {
	// TODO: push event onto device's event queue
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::get_param_value& msg) -> void {
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == scuff_plugin_type_clap) {
		if (const auto value = clap::main::get_param_value(*app, dev_id, msg.param_idx)) {
			app->msg_sender.enqueue(scuff::msg::out::return_param_value{*value, msg.callback});
		}
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::get_param_value_text& msg) -> void {
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == scuff_plugin_type_clap) {
		const auto text = clap::main::get_param_value_text(*app, dev_id, msg.param_idx, msg.value);
		app->msg_sender.enqueue(scuff::msg::out::return_param_value_text{text, msg.callback});
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::set_sample_rate& msg) -> void {
	const auto m = app->model.lock_read();
	for (const auto& dev : m.devices) {
		set_sample_rate(app, dev, msg.sr);
	}
}

static
auto process_input_msg(sbox::app* app, const scuff::msg::in::msg& msg) -> void {
	fast_visit([app](const auto& msg) { process_input_msg_(app, msg); }, msg);
}

static
auto process_messages(sbox::app* app) -> void {
	try {
		const auto receive = [app](std::byte* bytes, size_t count) -> size_t {
			return app->shm_sbox.receive_bytes(bytes, count);
		};
		const auto send = [app](const std::byte* bytes, size_t count) -> size_t {
			return app->shm_sbox.send_bytes(bytes, count);
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

} // scuff::sbox::main