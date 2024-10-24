#pragma once

#include "audio.hpp"
#include "log.hpp"
#include "os.hpp"
#include <format>
#include <immer/vector_transient.hpp>

namespace scuff::sbox::main {

[[nodiscard]] static
auto get_device_type(const sbox::app& app, id::device dev_id) -> plugin_type {
	return app.model.read().devices.at(dev_id).type;
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
auto activate(sbox::app* app, const sbox::device& dev, double sr) -> void {
	if (dev.type == plugin_type::clap) {
		if (!clap::main::activate(app, dev.id, sr)) {
			app->msg_sender.enqueue(scuff::msg::out::report_error{std::format("Failed to activate device {}", dev.id.value)});
		}
		return;
	}
}

static
auto deactivate(sbox::app* app, const sbox::device& dev) -> void {
	if (dev.type == plugin_type::clap) {
		clap::main::deactivate(app, dev.id);
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::clean_shutdown& msg) -> void {
	log(app, "msg::in::clean_shutdown:");
	// TOODOO: msg::in::clean_shutdown
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::close_all_editors& msg) -> void {
	log(&app->debug_ui, "msg::in::close_all_editors:");
	const auto devices = app->model.read().devices;
	for (const auto& dev : devices) {
		if (dev.ui.window) {
			window_hide(dev.ui.window);
		}
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::crash& msg) -> void {
	log(app, "msg::in::device_crash:");
	// Crash the process. This is used for testing the
	// way the client responds to sandbox crashes.
	double* x{}; 
	*x = 0;
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_create& msg) -> void {
	log(app, "msg::in::device_create:");
	const auto dev_id = id::device{msg.dev_id};
	try {
		if (msg.type == plugin_type::clap) {
			clap::main::create_device(app, dev_id, msg.plugfile_path, msg.plugin_id, msg.callback);
			app->model.update_publish([dev_id](model&& m){
				m.device_processing_order = make_device_processing_order(m.devices);
				return m;
			});
			const auto dev = app->model.read().devices.at(dev_id);
			if (app->active) {
				activate(app, dev, app->sample_rate);
			}
			dev.service->window_listener = {app, dev_id};
			app->msg_sender.enqueue(scuff::msg::out::device_create_success{dev_id.value, dev.service->shm.seg.id.data(), msg.callback});
			return;
		}
		else {
			// Not implemented yet
			throw std::runtime_error("Unsupported device type");
		}
	}
	catch (const std::exception& err) {
		app->msg_sender.enqueue(scuff::msg::out::device_create_fail{dev_id.value, err.what(), msg.callback});
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_connect& msg) -> void {
	log(app, "msg::in::device_connect:");
	app->model.update_publish([msg](model&& m){
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
		return m;
	});
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_disconnect& msg) -> void {
	log(app, "msg::in::device_disconnect:");
	app->model.update_publish([msg](model&& m){
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
		return m;
	});
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_erase& msg) -> void {
	log(app, "msg::in::device_erase:");
	app->model.update_publish([msg](model&& m){
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
		return m;
	});
}

static
auto gui_hide(sbox::app* app, sbox::device dev) -> void {
	if (!dev.ui.window) {
		return;
	}
	os::shutdown_editor_window(app, dev);
	switch (dev.type) {
		case plugin_type::clap: { clap::main::shutdown_editor_window(app, dev); break; }
		case plugin_type::vst3: { /* Not implemented yet. */ break; }
	}
	window_hide(dev.ui.window);
	app->model.update([dev](model&& m){
		m.devices = m.devices.insert(dev);
		return m;
	});
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_hide& msg) -> void {
	log(app, "msg::in::device_gui_hide:");
	const auto devices = app->model.read().devices;
	auto device        = devices.at({msg.dev_id});
	gui_hide(app, device);
}

static
auto on_native_window_resize_impl(sbox::app* app, const sbox::device& dev, window_size_f window_size) -> void {
	switch (dev.type) {
		case plugin_type::clap: { clap::main::on_native_window_resize(app, dev, window_size); break; }
		case plugin_type::vst3: { /* Not implemented yet. */ break; }
		default:                { assert (false); break; }
	}
}

static
auto on_native_window_close(device_window_listener* dwl, Event* event) -> void {
	const auto& device = dwl->app->model.read().devices.at(dwl->dev_id);
	gui_hide(dwl->app, device);
	dwl->app->msg_sender.enqueue(scuff::msg::out::device_editor_visible_changed{dwl->dev_id.value, false});
}

static
auto on_native_window_resize(device_window_listener* dwl, Event* event) -> void {
	const auto size = event_params(event, EvSize);
	log_printf("on_window_resize: %f,%f", size->width, size->height);
	const auto m    = dwl->app->model.read();
	const auto& dev = m.devices.at(dwl->dev_id);
	on_native_window_resize_impl(dwl->app, dev, {size->width, size->height});
}

[[nodiscard]] static
auto create_gui(sbox::app* app, const sbox::device& dev) -> sbox::create_gui_result {
	switch (dev.type) {
		case plugin_type::clap: { return clap::main::create_gui(app, dev); }
		case plugin_type::vst3: { /* Not implemented yet. */ return {}; }
		default:                { assert (false); return {}; }
	}
}

[[nodiscard]] static
auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> bool {
	os::setup_editor_window(app, dev);
	switch (dev.type) {
		case plugin_type::clap: { return clap::main::setup_editor_window(app, dev); }
		case plugin_type::vst3: { /* Not implemented yet. */ return false; }
		default:                { assert (false); return false; }
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_show& msg) -> void {
	log(app, "msg::in::device_gui_show:");
	const auto devices   = app->model.read().devices;
	auto device          = devices.at({msg.dev_id});
	const auto has_gui   = device.service->shm.data->flags.value & shm::device_flags::has_gui;
	if (!has_gui) {
		log(app, "Device %d has no GUI", device.id.value);
		return;
	}
	if (device.ui.window) {
		window_destroy(&device.ui.window);
	}
	const auto result = create_gui(app, device);
	if (!result.success) {
		log(app, "Failed to create GUI for device %d", device.id.value);
		return;
	}
	uint32_t window_flags = ekWINDOW_STDRES;
	if (!result.resizable) {
		window_flags &= ~ekWINDOW_RESIZE;
	}
	window_flags &= ~ekWINDOW_MAX;
	window_flags &= ~ekWINDOW_MIN;
	device.ui.window = window_create(window_flags);
	device.ui.panel  = panel_create();
	device.ui.layout = layout_create(1, 1);
	device.ui.view   = view_create();
	layout_view(device.ui.layout, device.ui.view, 0, 0);
	if (!result.resizable) {
		view_size(device.ui.view, S2Df(float(result.width), float(result.height)));
	}
	panel_layout(device.ui.panel, device.ui.layout);
	window_panel(device.ui.window, device.ui.panel);
	window_OnClose(device.ui.window, listener(&device.service->window_listener, on_native_window_close, device_window_listener));
	window_OnResize(device.ui.window, listener(&device.service->window_listener, on_native_window_resize, device_window_listener));
	if (result.resizable) {
		window_size(device.ui.window, S2Df(float(result.width), float(result.height)));
	}
	window_show(device.ui.window);
	if (!setup_editor_window(app, device)) {
		log(app, "Failed to setup clap editor window");
	}
	app->model.update([device](model&& m){
		m.devices = m.devices.insert(device);
		return m;
	});
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_load& msg) -> void {
	log(app, "msg::in::device_load:");
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		if (clap::main::load(app, dev_id, msg.state)) {
			app->msg_sender.enqueue(scuff::msg::out::device_load_success{dev_id.value});
		}
		else {
			app->msg_sender.enqueue(scuff::msg::out::device_load_fail{dev_id.value, "Failed to load device state for some unknown reason."});
		}
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_save& msg) -> void {
	log(app, "msg::in::device_save:");
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		const auto state = clap::main::save(app, dev_id);
		if (state.empty()) {
			app->msg_sender.enqueue(scuff::msg::out::report_error{"Failed to save device state"});
			return;
		}
		app->msg_sender.enqueue(scuff::msg::out::return_state{state, msg.callback});
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_set_render_mode& msg) -> void {
	log(app, "msg::in::device_set_render_mode:");
	// TOODOO: msg::in::device_set_render_mode
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::event& msg) -> void {
	log(app, "msg::in::event:");
	const auto dev_id  = id::device{msg.dev_id};
	const auto devices = app->model.read().devices;
	const auto dev     = devices.at(dev_id);
	dev.service->input_events_from_main.enqueue(msg.event);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::get_param_value& msg) -> void {
	log(app, "msg::in::get_param_value:");
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		if (const auto value = clap::main::get_param_value(*app, dev_id, {msg.param_idx})) {
			app->msg_sender.enqueue(scuff::msg::out::return_param_value{*value, msg.callback});
		}
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::get_param_value_text& msg) -> void {
	log(app, "msg::in::get_param_value_text:");
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		const auto text = clap::main::get_param_value_text(*app, dev_id, {msg.param_idx}, msg.value);
		app->msg_sender.enqueue(scuff::msg::out::return_param_value_text{text, msg.callback});
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::activate& msg) -> void {
	log(app, "msg::in::activate: %f", msg.sr);
	audio::start(app);
	const auto m = app->model.read();
	for (const auto& dev : m.devices) {
		activate(app, dev, msg.sr);
	}
	app->msg_sender.enqueue(msg::out::confirm_activated{});
	app->sample_rate    = msg.sr;
	app->active         = true;
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::deactivate& msg) -> void {
	log(app, "msg::in::deactivate:");
	const auto m = app->model.read();
	for (const auto& dev : m.devices) {
		deactivate(app, dev);
	}
	audio::stop(app);
	app->active = false;
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::heartbeat& msg) -> void {
	//log(app, "msg::in::heartbeat:");
	app->last_heartbeat = std::chrono::steady_clock::now();
}

static
auto process_input_msg(sbox::app* app, const scuff::msg::in::msg& msg) -> void {
	fast_visit([app](const auto& msg) { process_input_msg_(app, msg); }, msg);
}

static
auto process_messages(sbox::app* app) -> void {
	try {
		const auto receive = [app](std::byte* bytes, size_t count) -> size_t {
			return shm::receive_bytes_from_client(app->shm_sbox, bytes, count);
		};
		const auto send = [app](const std::byte* bytes, size_t count) -> size_t {
			return shm::send_bytes_to_client(app->shm_sbox, bytes, count);
		};
		const auto& input_msgs = app->msg_receiver.receive(receive);
		for (const auto& msg : input_msgs) {
			process_input_msg(app, msg);
		}
		app->msg_sender.send(send);
	}
	catch (const std::exception& err) {
		log(app, "Error in process_messages(): %s", err.what());
		app->msg_sender.enqueue(scuff::msg::out::report_error{err.what()});
	}
}

} // scuff::sbox::main