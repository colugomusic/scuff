#pragma once

#include "audio.hpp"
#include "gui.hpp"
#include "op.hpp"

namespace scuff::sbox::main {

[[nodiscard]] static
auto get_device_type(const sbox::app& app, id::device dev_id) -> plugin_type {
	return app.model.read(ez::main).devices.at(dev_id).type;
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::close_all_editors& msg) -> void {
	DLOG_S(INFO) << "msg::in::close_all_editors:";
	const auto devices = app->model.read(ez::main).devices;
	for (const auto& dev : devices) {
		if (dev.ui.window) {
			edwin::set(dev.ui.window, edwin::hide);
			app->msgs_out.lock()->push_back(scuff::msg::out::device_editor_visible_changed{dev.id.value, false, (int64_t)(edwin::get_native_handle(*dev.ui.window).value)});
		}
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::crash& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_crash:";
	// Crash the process. This is used for testing the
	// way the client responds to sandbox crashes.
	double* x{}; 
	*x = 0;
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_create& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_create:";
	try {
		const auto dev = op::device_create(app, msg.type, id::device{msg.dev_id}, msg.plugfile_path, msg.plugin_id);
		app->msgs_out.lock()->push_back(scuff::msg::out::device_create_success{msg.dev_id, dev.service->shm.seg.id.data(), msg.callback});
	}
	catch (const std::exception& err) {
		app->msgs_out.lock()->push_back(scuff::msg::out::device_create_fail{msg.dev_id, err.what(), msg.callback});
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_connect& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_connect:";
	op::device_connect(app, {msg.out_dev_id}, msg.out_port, {msg.in_dev_id}, msg.in_port);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_disconnect& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_disconnect:";
	op::device_disconnect(app, {msg.out_dev_id}, msg.out_port, {msg.in_dev_id}, msg.in_port);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_erase& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_erase:";
	op::device_erase(app, {msg.dev_id});
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_hide& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_gui_hide:";
	const auto devices = app->model.read(ez::main).devices;
	auto device        = devices.at({msg.dev_id});
	gui::hide(app, device);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_gui_show& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_gui_show:";
	gui::show(app, {msg.dev_id}, {[]{}});
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_load& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_load:";
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		if (clap::main::load(app, dev_id, msg.state)) {
			app->msgs_out.lock()->push_back(scuff::msg::out::device_load_success{dev_id.value});
		}
		else {
			app->msgs_out.lock()->push_back(scuff::msg::out::device_load_fail{dev_id.value, "Failed to load device state for some unknown reason."});
		}
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::device_save& msg) -> void {
	DLOG_S(INFO) << "msg::in::device_save:";
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		const auto state = clap::main::save(app, dev_id);
		if (state.empty()) {
			app->msgs_out.lock()->push_back(scuff::msg::out::report_error{"Failed to save device state"});
			return;
		}
		app->msgs_out.lock()->push_back(scuff::msg::out::return_state{state, msg.callback});
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::set_render_mode& msg) -> void {
	DLOG_S(INFO) << "msg::in::set_render_mode:";
	// TOODOO: msg::in::set_render_mode
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::event& msg) -> void {
	DLOG_S(INFO) << "msg::in::event:";
	const auto dev_id  = id::device{msg.dev_id};
	const auto devices = app->model.read(ez::main).devices;
	const auto dev     = devices.at(dev_id);
	dev.service->input_events_from_main.enqueue(msg.event);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::get_param_value& msg) -> void {
	DLOG_S(INFO) << "msg::in::get_param_value:";
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		if (const auto value = clap::main::get_param_value(*app, dev_id, {msg.param_idx})) {
			app->msgs_out.lock()->push_back(scuff::msg::out::return_param_value{*value, msg.callback});
		}
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::get_param_value_text& msg) -> void {
	DLOG_S(INFO) << "msg::in::get_param_value_text:";
	const auto dev_id = id::device{msg.dev_id};
	const auto type = get_device_type(*app, dev_id);
	if (type == plugin_type::clap) {
		const auto text = clap::main::get_param_value_text(*app, dev_id, {msg.param_idx}, msg.value);
		app->msgs_out.lock()->push_back(scuff::msg::out::return_param_value_text{text, msg.callback});
		return;
	}
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::activate& msg) -> void {
	DLOG_S(INFO) << "msg::in::activate: " << msg.sr;
	op::activate(app, msg.sr);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::deactivate& msg) -> void {
	DLOG_S(INFO) << "msg::in::deactivate:";
	op::deactivate(app);
}

static
auto process_input_msg_(sbox::app* app, const scuff::msg::in::heartbeat& msg) -> void {
	//app->logger->debug("msg::in::heartbeat:");
	app->last_heartbeat = std::chrono::steady_clock::now();
}

static
auto process_input_msg(sbox::app* app, const scuff::msg::in::msg& msg) -> void {
	fast_visit([app](const auto& msg) { process_input_msg_(app, msg); }, msg);
}

static
auto process_client_messages(sbox::app* app) -> void {
	try {
		const auto receive = [app](std::byte* bytes, size_t count) -> size_t {
			return shm::receive_bytes_from_client(app->shm_sbox, bytes, count);
		};
		const auto send = [app](const std::byte* bytes, size_t count) -> size_t {
			return shm::send_bytes_to_client(app->shm_sbox, bytes, count);
		};
		const auto& input_msgs = app->client_msg_receiver.receive(receive);
		for (const auto& msg : input_msgs) {
			process_input_msg(app, msg);
		}
		app->client_msg_sender.send(send);
	}
	catch (const std::exception& err) {
		LOG_S(ERROR) << "Error in " << std::source_location::current().function_name() << ": " << err.what();
		app->msgs_out.lock()->push_back(scuff::msg::out::report_error{err.what()});
	}
}

} // scuff::sbox::main