#pragma once

#include "audio.hpp"
#include "gui.hpp"
#include "op.hpp"

namespace scuff::sbox {

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::close_all_editors& msg) -> void {
	fu::debug_log("INFO: msg::in::close_all_editors");
	const auto devices = app->model.read(ez::main).devices;
	for (const auto& dev : devices) {
		if (dev.ui.window) {
			edwin::set(dev.ui.window, edwin::hide);
			fu::debug_log("msg out -> device_editor_visible_changed");
			app->msgs_out.lock()->push_back(scuff::msg::out::device_editor_visible_changed{dev.id.value, false, (int64_t)(edwin::get_native_handle(*dev.ui.window).value)});
		}
	}
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::crash& msg) -> void {
	fu::debug_log("INFO: msg::in::crash");
	// Crash the process. This is used for testing the
	// way the client responds to sandbox crashes.
	double* x{}; 
	*x = 0;
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::device_create& msg) -> void {
	fu::debug_log("INFO: msg::in::device_create");
	try {
		const auto dev = op::device_create(ez::main, app, msg.type, id::device{msg.dev_id}, msg.plugfile_path, msg.plugin_id);
		op::set_render_mode(ez::main, app, dev.id, app->render_mode);
		fu::debug_log("msg out -> device_create_success");
		fu::debug_log("msg out -> device_flags");
		fu::debug_log("msg out -> device_port_info");
		fu::debug_log("msg out -> device_param_info");
		app->msgs_out.lock()->push_back(scuff::msg::out::device_create_success{msg.dev_id, dev.service->shm.seg.id.data(), msg.callback});
		app->msgs_out.lock()->push_back(scuff::msg::out::device_flags{msg.dev_id, dev.flags.value});
		app->msgs_out.lock()->push_back(scuff::msg::out::device_port_info{msg.dev_id, op::make_device_port_info(ez::main, *app, dev)});
		app->msgs_out.lock()->push_back(scuff::msg::out::device_param_info{msg.dev_id, op::make_client_param_info(dev)});
		fu::debug_log(std::format("INFO: Passing flags to client: {}", dev.flags.value));
	}
	catch (const std::exception& err) {
		fu::debug_log("msg out -> device_create_fail");
		app->msgs_out.lock()->push_back(scuff::msg::out::device_create_fail{msg.dev_id, err.what(), msg.callback});
	}
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::device_connect& msg) -> void {
	fu::debug_log("INFO: msg::in::device_connect");
	op::device_connect(ez::main, app, {msg.out_dev_id}, msg.out_port, {msg.in_dev_id}, msg.in_port);
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::device_disconnect& msg) -> void {
	fu::debug_log("INFO: msg::in::device_disconnect");
	op::device_disconnect(ez::main, app, {msg.out_dev_id}, msg.out_port, {msg.in_dev_id}, msg.in_port);
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::device_erase& msg) -> void {
	fu::debug_log("INFO: msg::in::device_erase");
	op::device_erase(ez::main, app, {msg.dev_id});
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::device_gui_hide& msg) -> void {
	fu::debug_log("INFO: msg::in::device_gui_hide");
	const auto devices = app->model.read(ez::main).devices;
	auto device        = devices.at({msg.dev_id});
	gui::hide(ez::main, app, device);
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::device_gui_show& msg) -> void {
	fu::debug_log("INFO: msg::in::device_gui_show");
	gui::show(ez::main, app, {msg.dev_id}, {[]{}});
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::device_load& msg) -> void {
	fu::debug_log("INFO: msg::in::device_load");
	const auto dev_id = id::device{msg.dev_id};
	const auto type = op::get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		if (clap::load(ez::main, app, dev_id, msg.state)) {
			fu::debug_log("msg out -> device_load_success");
			app->msgs_out.lock()->push_back(scuff::msg::out::device_load_success{dev_id.value, msg.callback});
		}
		else {
			fu::debug_log("msg out -> device_load_fail");
			app->msgs_out.lock()->push_back(scuff::msg::out::device_load_fail{dev_id.value, msg.callback});
		}
		return;
	}
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::device_request_state& msg) -> void {
	fu::debug_log("INFO: msg::in::device_request_state");
	auto state = op::save(ez::main, app, id::device{msg.dev_id});
	if (state.empty()) {
		fu::debug_log("msg out -> report_error");
		app->msgs_out.lock()->push_back(scuff::msg::out::report_error{"Failed to save device state"});
		return;
	}
	fu::debug_log("msg out -> return_requested_state");
	app->msgs_out.lock()->push_back(scuff::msg::out::return_requested_state{std::move(state), msg.callback});
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::panic& msg) -> void {
	fu::debug_log("INFO: msg::in::panic");
	const auto& devices = app->model.read(ez::main).devices;
	for (const auto& dev : devices) {
		op::panic(ez::main, app, dev.id, app->sample_rate);
	}
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::set_render_mode& msg) -> void {
	fu::debug_log("INFO: msg::in::set_render_mode");
	app->render_mode = msg.mode;
	const auto& devices = app->model.read(ez::main).devices;
	for (const auto& dev : devices) {
		op::set_render_mode(ez::main, app, dev.id, msg.mode);
	}
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::event& msg) -> void {
	fu::debug_log("INFO: msg::in::event");
	const auto dev_id  = id::device{msg.dev_id};
	const auto devices = app->model.read(ez::main).devices;
	const auto dev     = devices.at(dev_id);
	dev.service->input_events_from_main.enqueue(msg.event);
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::get_param_value& msg) -> void {
	fu::debug_log("INFO: msg::in::get_param_value");
	const auto dev_id = id::device{msg.dev_id};
	const auto type = op::get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		if (const auto value = clap::get_param_value(ez::main, *app, dev_id, {msg.param_idx})) {
			fu::debug_log("msg out -> return_param_value");
			app->msgs_out.lock()->push_back(scuff::msg::out::return_param_value{*value, msg.callback});
		}
		return;
	}
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::get_param_value_text& msg) -> void {
	fu::debug_log("INFO: msg::in::get_param_value_text");
	const auto dev_id = id::device{msg.dev_id};
	const auto type = op::get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		const auto text = clap::get_param_value_text(ez::main, *app, dev_id, {msg.param_idx}, msg.value);
		fu::debug_log("msg out -> return_param_value_text");
		app->msgs_out.lock()->push_back(scuff::msg::out::return_param_value_text{text, msg.callback});
		return;
	}
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::activate& msg) -> void {
	fu::debug_log(std::format("INFO: msg::in::activate: {}", msg.sr));
	op::activate(ez::main, app, msg.sr);
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::deactivate& msg) -> void {
	fu::debug_log("INFO: msg::in::deactivate");
	op::deactivate(ez::main, app);
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::heartbeat& msg) -> void {
	//app->logger->debug("msg::in::heartbeat:");
	app->last_heartbeat = std::chrono::steady_clock::now();
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::set_autosave_interval& msg) -> void {
	fu::debug_log("INFO: msg::in::set_autosave_interval");
	const auto dev_id = id::device{msg.dev_id};
	app->model.update(ez::main, [dev_id, interval = msg.interval_in_ms](scuff::sbox::model m) {
		m.devices = m.devices.update_if_exists(dev_id, [interval](scuff::sbox::device dev) {
			dev.autosave_interval =
				std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::milli>{interval});
			return dev;
		});
		return m;
	});
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::set_track_color& msg) -> void {
	fu::debug_log("INFO: msg::in::set_track_color");
	const auto dev_id = id::device{msg.dev_id};
	app->model.update(ez::main, [dev_id, color = msg.color](scuff::sbox::model m) {
		m.devices = m.devices.update_if_exists(dev_id, [color](scuff::sbox::device dev) {
			dev.track_color = color;
			return dev;
		});
		return m;
	});
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::set_track_name& msg) -> void {
	fu::debug_log("INFO: msg::in::set_track_name");
	const auto dev_id = id::device{msg.dev_id};
	app->model.update(ez::main, [dev_id, name = msg.name](scuff::sbox::model m) {
		m.devices = m.devices.update_if_exists(dev_id, [name](scuff::sbox::device dev) {
			dev.track_name = name;
			return dev;
		});
		return m;
	});
}

static
auto msg_from_client(ez::main_t, sbox::app* app, const scuff::msg::in::msg& msg) -> void {
	fast_visit([app](const auto& msg) { msg_from_client(ez::main, app, msg); }, msg);
}

static
auto process_client_messages(ez::main_t, sbox::app* app) -> void {
	try {
		const auto receive = [app](std::byte* bytes, size_t count) -> size_t {
			return shm::receive_bytes_from_client(app->shm_sbox, bytes, count);
		};
		const auto send = [app](const std::byte* bytes, size_t count) -> size_t {
			return shm::send_bytes_to_client(app->shm_sbox, bytes, count);
		};
		const auto& input_msgs = app->client_msg_receiver.receive(receive);
		for (const auto& msg : input_msgs) {
			msg_from_client(ez::main, app, msg);
		}
		app->client_msg_sender.send(send);
	}
	catch (const std::exception& err) {
		fu::log(std::format("ERROR: {}", err.what()), std::source_location::current());
		fu::debug_log("msg out -> report_error");
		app->msgs_out.lock()->push_back(scuff::msg::out::report_error{err.what()});
	}
}

} // scuff::sbox