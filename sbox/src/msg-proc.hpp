#pragma once

#include "clap.hpp"
#include <format>
#include <immer/vector_transient.hpp>

namespace scuff::sbox::main {

[[nodiscard]] static
auto get_device_type(const sbox::app& app, id::device dev_id) -> scuff_plugin_type {
	return app.working_model.lock()->devices.at(dev_id).type;
}

static
auto set_sample_rate(sbox::app* app, const sbox::device& dev, double sr) -> void {
	if (dev.type == scuff_plugin_type::clap) {
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
	const auto devices = app->working_model.lock()->devices;
	for (const auto& dev : devices) {
		if (dev.ui.window) {
			window_hide(dev.ui.window);
		}
	}
}

[[nodiscard]] static
auto make_shm_device(std::string_view instance_id, id::device dev_id) -> std::shared_ptr<shm::device> {
	return std::make_shared<shm::device>(bip::open_only, shm::segment::remove_when_done, shm::device::make_id(instance_id, dev_id));
}

[[nodiscard]] static
auto make_shm_audio_ports(std::string_view instance_id, id::sandbox sbox_id, id::device dev_id, uint64_t uid) -> std::shared_ptr<shm::device_audio_ports> {
	return std::make_shared<shm::device_audio_ports>(bip::open_only, shm::segment::remove_when_done, shm::device_audio_ports::make_id(instance_id, sbox_id, dev_id, uid));
}

[[nodiscard]] static
auto make_shm_param_info(std::string_view instance_id, id::sandbox sbox_id, id::device dev_id, uint64_t uid) -> std::shared_ptr<shm::device_param_info> {
	return std::make_shared<shm::device_param_info>(bip::open_only, shm::segment::remove_when_done, shm::device_param_info::make_id(instance_id, sbox_id, dev_id, uid));
}

[[nodiscard]] static
auto make_device_ext(sbox::app* app, id::device dev_id) -> device_external {
	device_external ext;
	ext.shm_device      = make_shm_device(app->instance_id, dev_id);
	ext.shm_audio_ports = make_shm_audio_ports(app->instance_id, app->options.sbox_id, dev_id, app->uid++);
	ext.shm_param_info  = make_shm_param_info(app->instance_id, app->options.sbox_id, dev_id, app->uid++);
	return ext;
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_create& msg) -> void {
	try {
		if (msg.type == scuff_plugin_type::clap) {
			auto dev      = scuff::sbox::device{};
			auto clap_dev = clap::main::create_device(app, dev.id, msg.plugfile_path, msg.plugin_id);
			dev.id        = {msg.dev_id};
			dev.type      = msg.type;
			dev.ext       = make_device_ext(app, dev.id);
			const auto m    = app->working_model.lock();
			m->devices      = m->devices.insert(dev);
			m->clap_devices = m->clap_devices.insert(clap_dev);
			app->published_model.set(*m);
			app->msg_sender.enqueue(scuff::msg::out::return_created_device{dev.id.value, dev.ext->shm_audio_ports->id().data(), dev.ext->shm_param_info->id().data(), msg.callback});
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
	const auto m       = app->working_model.lock();
	const auto in_dev  = m->devices.find({msg.in_dev_id});
	const auto out_dev = m->devices.find({msg.out_dev_id});
	if (in_dev) {
		auto dev   = *in_dev;
		auto conns = dev.input_conns.transient();
		while(conns.size() <= msg.in_port) { conns.push_back({}); }
		port_conn conn;
		conn.other_device     = {msg.out_dev_id};
		conn.other_port_index = msg.out_port;
		conn.external         = !out_dev;
		conns.set(msg.in_port, conn);
		dev.input_conns = conns.persistent();
		m->devices = m->devices.insert(dev);
	}
	if (out_dev) {
		auto dev   = *out_dev;
		auto conns = dev.output_conns.transient();
		while(conns.size() <= msg.out_port) { conns.push_back({}); }
		port_conn conn;
		conn.other_device     = {msg.in_dev_id};
		conn.other_port_index = msg.in_port;
		conn.external         = !in_dev;
		conns.set(msg.out_port, conn);
		dev.output_conns = conns.persistent();
		m->devices = m->devices.insert(dev);
	}
	app->published_model.set(*m);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_disconnect& msg) -> void {
	const auto m       = app->working_model.lock();
	const auto in_dev  = m->devices.find({msg.in_dev_id});
	const auto out_dev = m->devices.find({msg.out_dev_id});
	if (in_dev) {
		auto dev = *in_dev;
		dev.input_conns = dev.input_conns.set(msg.in_port, {});
		m->devices = m->devices.insert(dev);
	}
	if (out_dev) {
		auto dev = *out_dev;
		dev.output_conns = dev.output_conns.set(msg.out_port, {});
		m->devices = m->devices.insert(dev);
	}
	app->published_model.set(*m);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_erase& msg) -> void {
	const auto m = app->working_model.lock();
	const auto dev_id = id::device{msg.dev_id};
	const auto devices = m->devices;
	// Remove any internal connections to this device
	for (auto dev : devices) {
		for (size_t port_idx = 0; port_idx < dev.input_conns.size(); port_idx) {
			const auto& conn = dev.input_conns[port_idx];
			if (conn.other_device == dev_id) {
				dev.input_conns = dev.input_conns.set(port_idx, {});
			}
		}
		for (size_t port_idx = 0; port_idx < dev.output_conns.size(); port_idx) {
			const auto& conn = dev.output_conns[port_idx];
			if (conn.other_device == dev_id) {
				dev.output_conns = dev.output_conns.set(port_idx, {});
			}
		}
		m->devices = m->devices.insert(dev);
	}
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
	if (type == scuff_plugin_type::clap) {
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
	if (type == scuff_plugin_type::clap) {
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
	if (type == scuff_plugin_type::clap) {
		const auto text = clap::main::get_param_value_text(*app, dev_id, msg.param_idx, msg.value);
		app->msg_sender.enqueue(scuff::msg::out::return_param_value_text{text, msg.callback});
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::set_sample_rate& msg) -> void {
	const auto m = *app->working_model.lock();
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

} // scuff::sbox::main