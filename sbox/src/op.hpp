#pragma once

#include "clap.hpp"
#include <format>
#include <immer/vector_transient.hpp>

namespace scuff::sbox::op {

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

[[nodiscard]] static
auto get_latency(ez::main_t, const app& app, const device& dev) -> uint32_t {
	if (dev.type == plugin_type::clap) {
		return clap::get_latency(ez::main, app, dev.id);
	}
	throw std::runtime_error("Unsupported device type");
}

[[nodiscard]] static
auto activate(ez::main_t, sbox::app* app, const sbox::device& dev, double sr) -> bool {
	if (dev.type == plugin_type::clap) {
		if (!clap::activate(ez::main, app, dev.id, sr)) {
			app->msgs_out.lock()->push_back(scuff::msg::out::report_error{std::format("Failed to activate device {}", dev.id.value)});
			return false;
		}
		return true;
	}
	return false;
}

static
auto activate(ez::main_t, sbox::app* app, double sr) -> void {
	start_audio(ez::main, app);
	const auto m = app->model.read(ez::main);
	for (const auto& dev : m.devices) {
		if (activate(ez::main, app, dev, sr)) {
			app->msgs_out.lock()->push_back(msg::out::device_latency{dev.id.value, get_latency(ez::main, *app, dev)});
		}
	}
	app->msgs_out.lock()->push_back(msg::out::confirm_activated{});
	app->sample_rate = sr;
	app->active      = true;
}

static
auto deactivate(ez::main_t, sbox::app* app, const sbox::device& dev) -> void {
	if (dev.type == plugin_type::clap) {
		clap::deactivate(ez::main, app, dev.id);
	}
}

static
auto deactivate(ez::main_t, sbox::app* app) -> void {
	const auto m = app->model.read(ez::main);
	for (const auto& dev : m.devices) {
		deactivate(ez::main, app, dev);
	}
	stop_audio(ez::main, app);
	app->active = false;
}

static
auto device_connect(ez::main_t, sbox::app* app, id::device out_dev_id, size_t out_port, id::device in_dev_id, size_t in_port) -> void {
	app->model.update_publish(ez::main, [out_dev_id, out_port, in_dev_id, in_port](model&& m){
		auto in_dev_ptr  = m.devices.find(in_dev_id);
		auto out_dev_ptr = m.devices.find(out_dev_id);
		if (!in_dev_ptr)  { throw std::runtime_error(std::format("Input device {} doesn't exist in this sandbox!", in_dev_id.value)); }
		if (!out_dev_ptr) { throw std::runtime_error(std::format("Output device {} doesn't exist in this sandbox!", out_dev_id.value)); }
		auto out_dev = *out_dev_ptr;
		port_conn conn;
		conn.other_device     = in_dev_id;
		conn.other_port_index = in_port;
		conn.this_port_index  = out_port;
		out_dev.output_conns = out_dev.output_conns.push_back(conn);
		m.devices                 = m.devices.insert(out_dev);
		m.device_processing_order = make_device_processing_order(m.devices);
		return m;
	});
}

static
auto device_disconnect(ez::main_t, sbox::app* app, id::device out_dev_id, size_t out_port, id::device in_dev_id, size_t in_port) -> void {
	app->model.update_publish(ez::main, [out_dev_id, out_port, in_dev_id, in_port](model&& m) {
		const auto in_dev_ptr  = m.devices.find(in_dev_id);
		const auto out_dev_ptr = m.devices.find(out_dev_id);
		if (!in_dev_ptr)  { throw std::runtime_error(std::format("Input device {} doesn't exist in this sandbox!", in_dev_id.value)); }
		if (!out_dev_ptr) { throw std::runtime_error(std::format("Output device {} doesn't exist in this sandbox!", out_dev_id.value)); }
		auto out_dev = *out_dev_ptr;
		port_conn conn;
		conn.other_device     = in_dev_id;
		conn.other_port_index = in_port;
		conn.this_port_index  = out_port;
		auto pos = std::find(out_dev.output_conns.begin(), out_dev.output_conns.end(), conn);
		if (pos == out_dev.output_conns.end()) {
			throw std::runtime_error(std::format("Output device {} port {} is not connected to input device {} port {}!", out_dev_id.value, out_port, in_dev_id.value, in_port));
		}
		out_dev.output_conns      = out_dev.output_conns.erase(pos.index());
		m.devices                 = m.devices.insert(out_dev);
		m.device_processing_order = make_device_processing_order(m.devices);
		return m;
	});
}

static
auto device_create(ez::main_t, sbox::app* app, plugin_type type, id::device dev_id, std::string_view plugfile_path, std::string_view plugin_id) -> sbox::device {
	if (type == plugin_type::clap) {
		clap::create_device(ez::main, app, dev_id, plugfile_path, plugin_id);
		app->model.update_publish(ez::main, [dev_id](model&& m){
			m.device_processing_order = make_device_processing_order(m.devices);
			return m;
		});
		const auto dev = app->model.read(ez::main).devices.at(dev_id);
		if (app->active) {
			if (activate(ez::main, app, dev, app->sample_rate)) {
				app->msgs_out.lock()->push_back(msg::out::device_latency{dev.id.value, get_latency(ez::main, *app, dev)});
			}
		}
		return dev;
	}
	throw std::runtime_error("Unsupported device type");
}

static
auto device_erase(ez::main_t, sbox::app* app, id::device dev_id) -> void {
	app->model.update_publish(ez::main, [app, dev_id](model&& m){
		const auto devices = m.devices;
		const auto dev = devices.at(dev_id);
		switch (dev.type) {
			case plugin_type::clap: { clap::destroy(ez::main, m, dev); break; }
			default:                { throw std::runtime_error("Unsupported device type"); }
		}
		// Remove any internal connections to this device
		for (auto dev : devices) {
			for (auto pos = dev.output_conns.begin(); pos != dev.output_conns.end(); ++pos) {
				if (pos->other_device == dev_id) {
					dev.output_conns = dev.output_conns.erase(pos.index());
				}
			}
			m.devices = m.devices.insert(dev);
		}
		m.devices                 = m.devices.erase({dev_id});
		m.clap_devices            = m.clap_devices.erase({dev_id});
		m.device_processing_order = make_device_processing_order(m.devices);
		return m;
	});
}

auto panic(ez::main_t, sbox::app* app, id::device dev_id, double sr) -> void {
	const auto m = app->model.read(ez::main);
	const auto dev = m.devices.at(dev_id);
	const auto& service = dev.service;
	// Send "all sounds off" midi message
	scuff::events::midi event;
	event.header.time = 0;
	event.header.event_type = scuff::events::type::midi;
	event.data[0] = 0xB1;
	event.data[1] = 0x78;
	event.data[2] = 0;
	service->input_events_from_main.enqueue(event);
	switch (dev.type) {
		case plugin_type::clap: { clap::panic(ez::main, app, dev_id, sr); break; }
		default:                { throw std::runtime_error("Unsupported device type"); }
	}
}

auto set_render_mode(ez::main_t, sbox::app* app, id::device dev_id, scuff::render_mode mode) -> void {
	const auto m = app->model.read(ez::main);
	const auto dev = m.devices.at(dev_id);
	switch (dev.type) {
		case plugin_type::clap: { clap::set_render_mode(ez::main, app, dev_id, mode); break; }
		default:                { throw std::runtime_error("Unsupported device type"); }
	}
}

[[nodiscard]]
auto make_client_param_info(const sbox::device& dev) -> std::vector<client_param_info> {
	std::vector<client_param_info> client_infos;
	for (const auto& info : dev.param_info) {
		client_param_info client_info;
		client_info.default_value = info.default_value;
		client_info.id            = info.id;
		client_info.flags         = info.flags;
		client_info.name          = info.name;
		client_info.max_value     = info.max_value;
		client_info.min_value     = info.min_value;
		client_infos.push_back(client_info);
	}
	return client_infos;
}

[[nodiscard]] static
auto make_device_port_info(ez::main_t, const sbox::app& app, const sbox::device& dev) -> device_port_info {
	if (dev.type == plugin_type::clap) {
		return clap::make_device_port_info(ez::main, app, dev.id);
	}
	throw std::runtime_error("Unsupported device type");
}

} // scuff::sbox::op
