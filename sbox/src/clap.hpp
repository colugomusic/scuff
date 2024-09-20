#pragma once

#include "data.hpp"
#include "common/shm.hpp"
#include "common/visit.hpp"
#include <optional>
#include <ranges>

namespace scuff::sbox::clap {

[[nodiscard]] static
auto is_flag_set(uint32_t flags, uint32_t flag) -> bool {
	return (flags & flag) == flag;
}

[[nodiscard]] static
auto unset_flag(uint32_t flags, uint32_t flag) -> uint32_t {
	return flags & ~flag;
}

[[nodiscard]] static
auto is_flag_set(const device_atomic_flags& flags, uint32_t flag) -> bool {
	return is_flag_set(flags.value.load(std::memory_order_relaxed), flag);
}

static
auto set_flags(device_atomic_flags* atomic_flags, uint32_t flags_to_set) -> void {
	auto old_flags = atomic_flags->value.load(std::memory_order_relaxed);
	auto new_flags = old_flags | flags_to_set;
	while (!atomic_flags->value.compare_exchange_weak(old_flags, new_flags, std::memory_order_relaxed)) {
		new_flags = old_flags | flags_to_set;
	}
}

static
auto unset_flags(device_atomic_flags* atomic_flags, uint32_t flags_to_unset) -> void {
	auto old_flags = atomic_flags->value.load(std::memory_order_relaxed);
	auto new_flags = old_flags & ~flags_to_unset;
	while (!atomic_flags->value.compare_exchange_weak(old_flags, new_flags, std::memory_order_relaxed)) {
		new_flags = old_flags & ~flags_to_unset;
	}
}

[[nodiscard]] static
auto is_active(const device& device) -> bool {
	return is_flag_set(device.ext.data->atomic_flags, device_atomic_flags::active);
}

[[nodiscard]] static
auto is_processing(const device& device) -> bool {
	return is_flag_set(device.ext.data->atomic_flags, device_atomic_flags::processing);
}

[[nodiscard]] static
auto is_scheduled_to_process(const device& device) -> bool {
	return is_flag_set(device.ext.data->atomic_flags, device_atomic_flags::schedule_process);
}

static
auto send_msg(const device& dev, const clap::device_msg::msg& msg) -> void {
	dev.ext.data->msg_q.enqueue(msg);
}

static
auto send_msg(sbox::app* app, id::device dev_id, const clap::device_msg::msg& msg) -> void {
	const auto m = app->model.lockfree_read();
	if (const auto dev = m->clap_devices.find(dev_id)) {
		send_msg(*dev, msg);
	}
}

static
auto log(sbox::app* app, id::device dev_id, clap_log_severity severity, const char* msg) -> void {
	send_msg(app, dev_id, device_msg::log_begin{severity});
	// Send the message in chunks of 64 characters
	const auto len        = std::strlen(msg);
	const auto chunk_size = device_msg::log_text::MAX;
	for (size_t i = 0; i < len; i += chunk_size) {
		const auto chunk = std::string_view{msg + i, std::min(chunk_size, len - i)};
		send_msg(app, dev_id, device_msg::log_text{chunk.data()});
	}
	send_msg(app, dev_id, device_msg::log_end{});
}

[[nodiscard]] static
auto get_extension(sbox::app* app, id::device dev_id, const char* extension_id) -> const void* {
	const auto m   = app->model.lock_read();
	const auto dev = m.clap_devices.at(dev_id);
	if (extension_id == std::string_view{CLAP_EXT_AUDIO_PORTS})         { return &dev.iface->host.audio_ports; }
	if (extension_id == std::string_view{CLAP_EXT_CONTEXT_MENU})        { return nullptr; } // Not implemented yet &iface.context_menu; }
	if (extension_id == std::string_view{CLAP_EXT_CONTEXT_MENU_COMPAT}) { return nullptr; } // Not implemented yet &iface.context_menu; }
	if (extension_id == std::string_view{CLAP_EXT_GUI})                 { return &dev.iface->host.gui; }
	if (extension_id == std::string_view{CLAP_EXT_LATENCY})             { return &dev.iface->host.latency; }
	if (extension_id == std::string_view{CLAP_EXT_LOG})                 { return &dev.iface->host.log; }
	if (extension_id == std::string_view{CLAP_EXT_PARAMS})              { return &dev.iface->host.params; }
	if (extension_id == std::string_view{CLAP_EXT_PRESET_LOAD})         { return nullptr; } // Not implemented yet &iface.preset_load; }
	if (extension_id == std::string_view{CLAP_EXT_PRESET_LOAD_COMPAT})  { return nullptr; } // Not implemented yet &iface.preset_load; }
	if (extension_id == std::string_view{CLAP_EXT_STATE})               { return &dev.iface->host.state; }
	if (extension_id == std::string_view{CLAP_EXT_THREAD_CHECK})        { return &dev.iface->host.thread_check; }
	if (extension_id == std::string_view{CLAP_EXT_TRACK_INFO})          { return &dev.iface->host.track_info; }
	if (extension_id == std::string_view{CLAP_EXT_TRACK_INFO_COMPAT})   { return &dev.iface->host.track_info; }
	if (extension_id == std::string_view{CLAP_EXT_TAIL}) {
		// Not supported at the moment because this extension is too under-specified
		// https://github.com/free-audio/clap/blob/main/include/clap/ext/tail.h
		return nullptr;
	}
	app->msg_sender.enqueue(scuff::msg::out::report_warning{std::format("Device '{}' requested an unsupported extension: {}", *dev.name, extension_id)});
	return nullptr;
}

[[nodiscard]] static
auto get_host_data(const clap_host_t* host) -> device_host_data& {
	return *static_cast<device_host_data*>(host->host_data);
}

static
auto cb_request_param_flush(sbox::app* app, id::device dev_id) -> void {
	const auto ext = app->model.lockfree_read()->clap_devices.at(dev_id).ext;
	ext.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_param_flush, std::memory_order_relaxed);
}

static
auto cb_request_process(sbox::app* app, id::device dev_id) -> void {
	const auto ext = app->model.lockfree_read()->clap_devices.at(dev_id).ext;
	ext.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_active | device_atomic_flags::schedule_process);
}

static
auto cb_request_restart(sbox::app* app, id::device dev_id) -> void {
	const auto ext = app->model.lockfree_read()->clap_devices.at(dev_id).ext;
	ext.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_restart);
}

static
auto cb_request_callback(sbox::app* app, id::device dev_id) -> void {
	const auto ext = app->model.lockfree_read()->clap_devices.at(dev_id).ext;
	ext.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_callback);
}

static
auto cb_gui_closed(sbox::app* app, id::device dev_id, bool was_destroyed) -> void {
	send_msg(app, dev_id, device_msg::gui_closed{was_destroyed});
}

static
auto cb_gui_request_hide(sbox::app* app, id::device dev_id) -> void {
	send_msg(app, dev_id, device_msg::gui_request_hide{});
}

static
auto cb_gui_request_show(sbox::app* app, id::device dev_id) -> void {
	send_msg(app, dev_id, device_msg::gui_request_show{});
}

static
auto cb_gui_request_resize(sbox::app* app, id::device dev_id, uint32_t width, uint32_t height) -> void {
	send_msg(app, dev_id, device_msg::gui_request_resize{width, height});
}

static
auto cb_gui_resize_hints_changed(sbox::app* app, id::device dev_id) -> void {
	send_msg(app, dev_id, device_msg::gui_resize_hints_changed{});
}

static
// Could be called from main thread or audio thread, but
// never both simultaneously, for the same device.
auto flush_device_events(const sbox::device& dev, const clap::device& clap_dev) -> void {
	const auto& input_events  = clap_dev.ext.audio->input_events;
	const auto& output_events = clap_dev.ext.audio->output_events;
	const auto& iface         = clap_dev.iface->plugin;
	if (!iface.params) {
		// May not actually be intialized
		return;
	}
	iface.params->flush(iface.plugin, &input_events, &output_events);
	dev.ext.shm_device->data->events_in.clear();
}

} // namespace scuff::sbox::clap

namespace scuff::sbox::clap::audio {

[[nodiscard]] static
auto can_render_audio(const clap::audio_buffers& buffers) -> bool {
	if (buffers.inputs.buffers.empty())                { return false; }
	if (buffers.outputs.buffers.empty())               { return false; }
	if (buffers.inputs.buffers[0].channel_count == 0)  { return false; }
	if (buffers.outputs.buffers[0].channel_count == 0) { return false; }
	return true;
}

[[nodiscard]] static
auto try_to_wake_up(const clap::device& dev) -> bool {
	unset_flags(&dev.ext.data->atomic_flags, device_atomic_flags::schedule_process);
	if (!dev.iface->plugin.plugin->start_processing(dev.iface->plugin.plugin)) {
		return false;
	}
	set_flags(&dev.ext.data->atomic_flags, device_atomic_flags::processing);
	return true;
}

[[nodiscard]] static
auto output_is_quiet(const shm::device_audio_ports& shm_audio_ports) -> bool {
	static constexpr auto THRESHOLD = 0.0001f;
	for (size_t i = 0; i < shm_audio_ports.output_count; i++) {
		const auto& buffer = shm_audio_ports.output_buffers[i];
		for (size_t j = 0; j < buffer.size(); j++) {
			const auto frame = buffer[j];
			if (std::abs(frame) > THRESHOLD) {
				return false;
			}
		}
	}
	return true;
}

static
auto go_to_sleep(const clap::device& dev) -> void {
	dev.iface->plugin.plugin->stop_processing(dev.iface->plugin.plugin);
	unset_flags(&dev.ext.data->atomic_flags, device_atomic_flags::processing);
}

[[nodiscard]] static
auto handle_audio_process_result(const shm::device_audio_ports& shm_audio_ports, const clap::device& dev, clap_process_status status) -> void {
	switch (status) {
		case CLAP_PROCESS_CONTINUE: {
			return;
		}
		case CLAP_PROCESS_CONTINUE_IF_NOT_QUIET: {
			if (output_is_quiet(shm_audio_ports)) {
				go_to_sleep(dev);
			}
			return;
		}
		default:
		case CLAP_PROCESS_ERROR:
		case CLAP_PROCESS_TAIL:
		case CLAP_PROCESS_SLEEP: {
			go_to_sleep(dev);
			return;
		}
	}
}

static
auto handle_event_process_result(const clap::device& dev, clap_process_status status) -> void {
	switch (status) {
		case CLAP_PROCESS_ERROR:
		case CLAP_PROCESS_CONTINUE: {
			return;
		}
		case CLAP_PROCESS_CONTINUE_IF_NOT_QUIET:
		case CLAP_PROCESS_TAIL:
		case CLAP_PROCESS_SLEEP: {
			go_to_sleep(dev);
			return;
		}
	}
}

static
auto process_audio_device(const sbox::device& dev, const clap::device& clap_dev) -> void {
	const auto& iface   = clap_dev.iface->plugin;
	const auto& process = clap_dev.ext.audio->process;
	auto& flags         = clap_dev.ext.data->atomic_flags;
	auto& audio_buffers = clap_dev.ext.audio->buffers;
	const auto status   = iface.plugin->process(iface.plugin, &process);
	handle_audio_process_result(*dev.ext.shm_audio_ports, clap_dev, status);
	dev.ext.shm_device->data->events_in.clear();
}

static
auto process_event_device(const sbox::device& dev, const clap::device& clap_dev) -> void {
	const auto& iface   = clap_dev.iface->plugin;
	const auto& process = clap_dev.ext.audio->process;
	auto& flags         = clap_dev.ext.data->atomic_flags;
	const auto status   = iface.plugin->process(iface.plugin, &process);
	handle_event_process_result(clap_dev, status);
	dev.ext.shm_device->data->events_in.clear();
}

auto process(const sbox::model& m, const sbox::device& dev) -> void {
	const auto& clap_dev = m.clap_devices.at(dev.id);
	const auto& iface    = clap_dev.iface->plugin;
	if (!is_active(clap_dev)) {
		return;
	}
	if (!is_processing(clap_dev)) {
		flush_device_events(dev, clap_dev);
		if (!is_scheduled_to_process(clap_dev)) {
			return;
		}
		if (!try_to_wake_up(clap_dev)) {
			return;
		}
	}
	if (iface.audio_ports) {
		if (can_render_audio(clap_dev.ext.audio->buffers)) {
			process_audio_device(dev, clap_dev);
			return;
		}
	}
	process_event_device(dev, clap_dev);
}

} // namespace scuff::sbox::clap::audio

namespace scuff::sbox::clap::main {

static
auto make_audio_buffers(std::span<shm::audio_buffer> shm_buffers, const std::vector<clap_audio_port_info_t>& port_info, audio_buffers_detail* out) -> void {
	out->arrays.resize(port_info.size());
	out->buffers.resize(port_info.size());
	for (size_t port_index = 0; port_index < port_info.size(); port_index++) {
		const auto& info = port_info[port_index];
		auto& arr = out->arrays[port_index];
		arr.resize(info.channel_count);
	}
	for (size_t port_index = 0; port_index < port_info.size(); port_index++) {
		const auto& info = port_info[port_index];
		auto& arr = out->arrays[port_index];
		auto& buf = out->buffers[port_index];
		for (uint32_t c = 0; c < info.channel_count; c++) {
			auto& vec = shm_buffers[(port_index * info.channel_count) + c];
			arr[c] = vec.data();
		}
		buf.channel_count = info.channel_count;
		buf.constant_mask = 0;
		buf.data32        = arr.data();
		buf.data64        = nullptr;
		buf.latency       = 0;
	}
}

static
auto make_audio_buffers(const shm::device_audio_ports& shm_ports, const audio_port_info& port_info, clap::audio_buffers* out) -> void {
	*out = {};
	make_audio_buffers(std::span{shm_ports.input_buffers, shm_ports.input_count}, port_info.inputs, &out->inputs);
	make_audio_buffers(std::span{shm_ports.output_buffers, shm_ports.output_count}, port_info.outputs, &out->outputs);
}

[[nodiscard]] static
auto retrieve_audio_port_info(const iface_plugin& iface) -> audio_port_info {
	auto out = audio_port_info{};
	if (iface.audio_ports) {
		const auto input_count  = iface.audio_ports->count(iface.plugin, true);
		const auto output_count = iface.audio_ports->count(iface.plugin, false);
		for (uint32_t i = 0; i < input_count; i++) {
			clap_audio_port_info_t info;
			iface.audio_ports->get(iface.plugin, i, true, &info);
			out.inputs.push_back(info);
		}
		for (uint32_t i = 0; i < output_count; i++) {
			clap_audio_port_info_t info;
			iface.audio_ports->get(iface.plugin, i, false, &info);
			out.outputs.push_back(info);
		}
	}
	return out;
}

[[nodiscard]] static
auto make_input_event_list(const shm::device& shm) -> clap_input_events_t {
	clap_input_events_t list;
	list.ctx = &shm.data->events_in;
	list.size = [](const clap_input_events_t* list) -> uint32_t {
		const auto& event_buffer = *static_cast<const shm::event_buffer*>(list->ctx);
		return static_cast<uint32_t>(event_buffer.size());
	};
	list.get = [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
		const auto& event_buffer = *static_cast<const shm::event_buffer*>(list->ctx);
		return &scuff::clap::convert(event_buffer[index]);
	};
	return list;
}

[[nodiscard]] static
auto make_output_event_list(const shm::device& shm) -> clap_output_events_t {
	clap_output_events_t list;
	list.ctx = &shm.data->events_in;
	list.try_push = [](const clap_output_events_t* list, const clap_event_header_t* event) -> bool {
		const auto event_buffer = static_cast<shm::event_buffer*>(list->ctx);
		if (const auto converted_event = scuff::clap::convert(*event)) {
			event_buffer->push_back(*converted_event);
		}
		return true;
	};
	return list;
}

static
// AUDIO DEVICE
auto initialize_process_struct_for_audio_device(const shm::device& shm, clap::device_ext_audio* audio) -> void {
	audio->input_events                = make_input_event_list(shm);
	audio->output_events               = make_output_event_list(shm);
	audio->process.frames_count        = SCUFF_VECTOR_SIZE;
	audio->process.audio_inputs_count  = static_cast<uint32_t>(audio->buffers.inputs.buffers.size());
	audio->process.audio_inputs        = audio->buffers.inputs.buffers.data();
	audio->process.audio_outputs_count = static_cast<uint32_t>(audio->buffers.outputs.buffers.size());
	audio->process.audio_outputs       = audio->buffers.outputs.buffers.data();
	audio->process.steady_time         = -1;
	audio->process.transport           = nullptr;
	audio->process.in_events           = &audio->input_events;
	audio->process.out_events          = &audio->output_events;
}

static
// EVENT-ONLY DEVICE
auto initialize_process_struct_for_event_device(const shm::device& shm, clap::device_ext_audio* audio) -> void {
	audio->input_events                = make_input_event_list(shm);
	audio->output_events               = make_output_event_list(shm);
	static auto dummy_buffer           = clap_audio_buffer_t{0};
	audio->process.frames_count        = SCUFF_VECTOR_SIZE;
	audio->process.audio_inputs_count  = 0;
	audio->process.audio_inputs        = &dummy_buffer;
	audio->process.audio_outputs_count = 0;
	audio->process.audio_outputs       = &dummy_buffer;
	audio->process.steady_time         = -1;
	audio->process.transport           = nullptr;
	audio->process.in_events           = &audio->input_events;
	audio->process.out_events          = &audio->output_events;
}

[[nodiscard]] static
auto init_audio(const sbox::device& dev, const clap::device& clap_dev) -> std::shared_ptr<const device_ext_audio> {
	auto out = device_ext_audio{};
	if (clap_dev.iface->plugin.audio_ports) {
		// AUDIO PLUGIN
		make_audio_buffers(*dev.ext.shm_audio_ports, clap_dev.ext.audio_port_info, &out.buffers);
		initialize_process_struct_for_audio_device(*dev.ext.shm_device, &out);
	}
	else {
		// EVENT-ONLY PLUGIN
		initialize_process_struct_for_event_device(*dev.ext.shm_device, &out);
	}
	return std::make_shared<const device_ext_audio>(std::move(out));
}

[[nodiscard]] static
auto init_audio(clap::device&& clap_dev, const sbox::device& dev) -> device {
	clap_dev.ext.audio = init_audio(dev, clap_dev);
	return clap_dev;
}

static
auto init_audio(sbox::app* app, id::device dev_id) -> void {
	const auto m                 = app->model.lock_write();
	auto dev                     = m->devices.at(dev_id);
	auto clap_dev                = m->clap_devices.at(dev_id);
	clap_dev.ext.audio_port_info = retrieve_audio_port_info(clap_dev.iface->plugin);
	clap_dev                     = init_audio(std::move(clap_dev), dev);
	m->clap_devices              = m->clap_devices.insert(clap_dev);
	app->model.lock_publish();
}

static
auto rescan_audio_ports(sbox::app* app, id::device dev_id, uint32_t flags) -> void {
	const auto requires_not_active =
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_CHANNEL_COUNT) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_FLAGS) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_PORT_TYPE) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_IN_PLACE_PAIR) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_LIST);
	const auto m   = app->model.lock_read();
	const auto dev = m.clap_devices.at(dev_id);
	if (requires_not_active) {
		if (is_active(dev)) {
			app->msg_sender.enqueue(scuff::msg::out::report_warning{std::format("Device '{}' tried to rescan audio ports while it is still active!", *dev.name)});
			return;
		}
	}
	init_audio(app, dev_id);
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_closed& msg) -> void {
	// TODO: process msg
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_request_hide& msg) -> void {
	// TODO: process msg
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_request_resize& msg) -> void {
	// TODO: process msg
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_request_show& msg) -> void {
	// TODO: process msg
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_resize_hints_changed& msg) -> void {
	// TODO: process msg
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::log_begin& msg) -> void {
	dev.ext.data->log_collector.severity = msg.severity;
	dev.ext.data->log_collector.chunks.clear();
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::log_end& msg) -> void {
	std::string text;
	for (const auto& chunk : dev.ext.data->log_collector.chunks) {
		text.append(chunk.c_str());
	}
	switch (*dev.ext.data->log_collector.severity) {
		default:
		case CLAP_LOG_DEBUG:
		case CLAP_LOG_INFO: {
			app->msg_sender.enqueue(scuff::msg::out::report_info{text});
			break;
		}
		case CLAP_LOG_HOST_MISBEHAVING:
		case CLAP_LOG_PLUGIN_MISBEHAVING:
		case CLAP_LOG_ERROR:
		case CLAP_LOG_FATAL: {
			app->msg_sender.enqueue(scuff::msg::out::report_error{text});
			break;
		}
		case CLAP_LOG_WARNING: {
			app->msg_sender.enqueue(scuff::msg::out::report_warning{text});
			break;
		}
	}
	dev.ext.data->log_collector.severity = std::nullopt;
	dev.ext.data->log_collector.chunks.clear();
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::log_text& msg) -> void {
	dev.ext.data->log_collector.chunks.push_back(msg.text);
}

static
auto process_msg(sbox::app* app, const device& dev, const clap::device_msg::msg& msg) -> void {
	fast_visit([app, dev](const auto& msg) { process_msg_(app, dev, msg); }, msg);
}

static
auto update(sbox::app* app) -> void {
	const auto m = app->model.lock_read();
	for (const auto& dev : m.clap_devices) {
		clap::device_msg::msg msg;
		while (dev.ext.data->msg_q.try_dequeue(msg)) {
			process_msg(app, dev, msg);
		}
	}
}

} // namespace scuff::sbox::clap::main

namespace scuff::sbox::clap::main {

[[nodiscard]] static
auto make_host_for_instance(device_host_data* host_data) -> iface_host {
	iface_host iface;
	iface.host.clap_version = CLAP_VERSION;
	iface.host.name         = "scuff-sbox";
	iface.host.url          = "https://github.com/colugomusic/scuff";
	iface.host.vendor       = "Moron Enterprises";
	iface.host.host_data    = host_data;
	iface.host.get_extension = [](const clap_host_t* host, const char* extension_id) -> const void* {
		const auto& hd = get_host_data(host);
		return get_extension(hd.app, hd.id, extension_id);
	};
	iface.host.request_callback = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_callback(hd.app, hd.id);
	};
	iface.host.request_process = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_process(hd.app, hd.id);
	};
	iface.host.request_restart = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_restart(hd.app, hd.id);
	};
	// AUDIO PORTS ______________________________________________________________
	iface.audio_ports.is_rescan_flag_supported = [](const clap_host* host, uint32_t flag) -> bool {
		return true;
	};
	iface.audio_ports.rescan = [](const clap_host* host, uint32_t flags) -> void {
		const auto& hd = get_host_data(host);
		main::rescan_audio_ports(hd.app, hd.id, flags);
	};
	// CONTEXT MENU _____________________________________________________________
	iface.context_menu.can_popup = [](const clap_host* host) -> bool {
		// Not implemented yet
		return false;
	};
	iface.context_menu.perform = [](const clap_host* host, const clap_context_menu_target_t* target, clap_id action_id) -> bool {
		// Not implemented yet
		return false;
	};
	iface.context_menu.populate = [](const clap_host* host, const clap_context_menu_target_t* target, const clap_context_menu_builder_t* builder) -> bool {
		// Not implemented yet
		return false;
	};
	iface.context_menu.popup = [](const clap_host* host, const clap_context_menu_target_t* target, int32_t screen_index, int32_t x, int32_t y) -> bool {
		// Not implemented yet
		return false;
	};
	// GUI ______________________________________________________________________
	iface.gui.closed = [](const clap_host* host, bool was_destroyed) -> void {
		const auto& hd = get_host_data(host);
		cb_gui_closed(hd.app, hd.id, was_destroyed);
	};
	iface.gui.request_hide = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_hide(hd.app, hd.id);
		return true;
	};
	iface.gui.request_show = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_show(hd.app, hd.id);
		return true;
	};
	iface.gui.request_resize = [](const clap_host* host, uint32_t width, uint32_t height) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_resize(hd.app, hd.id, width, height);
		return true;
	};
	iface.gui.resize_hints_changed = [](const clap_host* host) -> void {
		const auto& hd = get_host_data(host);
		cb_gui_resize_hints_changed(hd.app, hd.id);
	};
	// LATENCY __________________________________________________________________
	iface.latency.changed = [](const clap_host* host) -> void {
		// I don't do anything with this yet
	};
	// LOG ----------------------------------------------------------------------
	iface.log.log = [](const clap_host* host, clap_log_severity severity, const char* msg) -> void {
		const auto& hd = get_host_data(host);
		clap::log(hd.app, hd.id, severity, msg);
	};
	// PARAMS ___________________________________________________________________
	iface.params.clear = [](const clap_host* host, clap_id param_id, clap_param_clear_flags flags) -> void {
		// I don't do anything with this yet
	};
	iface.params.request_flush = [](const clap_host* host) -> void {
		const auto& hd = get_host_data(host);
		cb_request_param_flush(hd.app, hd.id);
	};
	iface.params.rescan = [](const clap_host* host, clap_param_rescan_flags flags) -> void {
		const auto& hd = get_host_data(host);
		// TODO: params.rescan: Figure out what we do here
	};
	// PRESET LOAD ______________________________________________________________
	iface.preset_load.loaded = [](const clap_host* host, uint32_t location_kind, const char* location, const char* load_key) -> void {
		// Not implemented yet
	};
	iface.preset_load.on_error = [](const clap_host* host, uint32_t location_kind, const char* location, const char* load_key, int32_t os_error, const char* msg) -> void {
		// Not implemented yet
	};
	// STATE ____________________________________________________________________
	iface.state.mark_dirty = [](const clap_host* host) -> void {
		// I don't do anything with this yet
	};
	// THREAD CHECK _____________________________________________________________
	iface.thread_check.is_audio_thread = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		return hd.app->main_thread_id != std::this_thread::get_id();
	};
	iface.thread_check.is_main_thread = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		return hd.app->main_thread_id == std::this_thread::get_id();
	};
	// TRACK INFO _______________________________________________________________
	iface.track_info.get = [](const clap_host* host, clap_track_info_t* info) -> bool {
		const auto& hd = get_host_data(host);
		// TODO: track_info.get: sort this out
		return true;
	};
	return iface;
}

template <typename T> [[nodiscard]] static
auto get_plugin_ext(const clap::iface_plugin& iface, const char* id, const char* fallback_id = nullptr) -> const T* {
	auto ptr = static_cast<const T*>(iface.plugin->get_extension(iface.plugin, id));
	if (!ptr && fallback_id) {
		ptr = static_cast<const T*>(iface.plugin->get_extension(iface.plugin, fallback_id));
	}
	return ptr;
}

static
auto get_extensions(clap::iface_plugin* iface) -> void {
	iface->audio_ports  = get_plugin_ext<clap_plugin_audio_ports_t>(*iface, CLAP_EXT_AUDIO_PORTS);
	iface->context_menu = get_plugin_ext<clap_plugin_context_menu_t>(*iface, CLAP_EXT_CONTEXT_MENU, CLAP_EXT_CONTEXT_MENU_COMPAT);
	iface->gui          = get_plugin_ext<clap_plugin_gui_t>(*iface, CLAP_EXT_GUI);
	iface->params       = get_plugin_ext<clap_plugin_params_t>(*iface, CLAP_EXT_PARAMS);
	iface->render       = get_plugin_ext<clap_plugin_render_t>(*iface, CLAP_EXT_RENDER);
	iface->state        = get_plugin_ext<clap_plugin_state_t>(*iface, CLAP_EXT_STATE);
	iface->tail         = get_plugin_ext<clap_plugin_tail_t>(*iface, CLAP_EXT_TAIL);
}

[[nodiscard]] static
auto get_window_api() -> const char* {
#if _WIN32
	return CLAP_WINDOW_API_WIN32;
#elif __APPLE__
	return CLAP_WINDOW_API_COCOA;
#else
	return CLAP_WINDOW_API_X11;
#endif
}

[[nodiscard]] static
auto init_gui(clap::device&& dev) -> clap::device {
	const auto& iface = dev.iface->plugin;
	if (iface.gui) {
		if (iface.gui->is_api_supported(iface.plugin, get_window_api(), false)) {
			dev.flags.value |= device_flags::has_gui;
			return dev;
		}
	}
	return dev;
}

[[nodiscard]] static
auto init_params(clap::device&& dev) -> clap::device {
	const auto& iface = dev.iface->plugin;
	if (iface.params) {
		const auto count = iface.params->count(iface.plugin);
		for (const auto i : std::ranges::views::iota(0u, count)) {
			clap_param_info_t info;
			if (iface.params->get_info(iface.plugin, i, &info)) {
				clap::param p;
				p.info = info;
				dev.params = dev.params.push_back(p);
			}
		}
		dev.flags.value |= device_flags::has_params;
	}
	return dev;
}

[[nodiscard]] static
auto make_ext_data(sbox::app* app, id::device id) -> std::shared_ptr<clap::device_ext_data> {
	auto data = std::make_shared<clap::device_ext_data>();
	data->host_data.app = app;
	data->host_data.id  = id;
	return data;
}

[[nodiscard]] static
auto make_shm_device(std::string_view instance_id, id::device dev_id) -> std::shared_ptr<shm::device> {
	return std::make_shared<shm::device>(bip::create_only, shm::device::make_id(instance_id, dev_id));
}

[[nodiscard]] static
auto make_shm_audio_ports(std::string_view instance_id, id::sandbox sbox_id, id::device dev_id, uint64_t uid, size_t input_count, size_t output_count) -> std::shared_ptr<shm::device_audio_ports> {
	return std::make_shared<shm::device_audio_ports>(bip::create_only, shm::device_audio_ports::make_id(instance_id, sbox_id, dev_id, uid), input_count, output_count);
}

[[nodiscard]] static
auto make_shm_param_info(std::string_view instance_id, id::sandbox sbox_id, id::device dev_id, uint64_t uid, size_t param_count) -> std::shared_ptr<shm::device_param_info> {
	return std::make_shared<shm::device_param_info>(bip::create_only, shm::device_param_info::make_id(instance_id, sbox_id, dev_id, uid), param_count);
}

static
auto create_device(sbox::app* app, id::device dev_id, std::string_view plugfile_path, std::string_view plugin_id, size_t callback) -> void {
	const auto entry = scuff::os::find_clap_entry(plugfile_path);
	if (!entry) {
		throw std::runtime_error("Couldn't resolve clap_entry");
	}
	if (!entry->init(plugfile_path.data())) {
		throw std::runtime_error("clap_plugin_entry.init failed");
	}
	const auto factory = reinterpret_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
	if (!factory) {
		entry->deinit();
		throw std::runtime_error("clap_plugin_entry.get_factory failed");
	}
	clap::iface iface;
	const auto ext_data = make_ext_data(app, dev_id);
	iface.host          = make_host_for_instance(&ext_data->host_data);
	iface.plugin.plugin = factory->create_plugin(factory, &iface.host.host, plugin_id.data());
	if (!iface.plugin.plugin) {
		entry->deinit();
		throw std::runtime_error("clap_plugin_factory.create_plugin failed");
	}
	get_extensions(&iface.plugin);
	auto dev                     = sbox::device{};
	auto clap_dev                = clap::device{};
	dev.id                       = dev_id;
	dev.type                     = scuff_plugin_type::clap;
	dev.ext.shm_device           = make_shm_device(app->instance_id, dev_id);
	clap_dev.ext.audio_port_info = retrieve_audio_port_info(iface.plugin);
	const auto audio_in_count    = clap_dev.ext.audio_port_info->inputs.size();
	const auto audio_out_count   = clap_dev.ext.audio_port_info->outputs.size();
	dev.ext.shm_audio_ports      = make_shm_audio_ports(app->instance_id, app->options.sbox_id, dev_id, app->uid++, audio_in_count, audio_out_count);
	clap_dev.id                  = dev_id;
	clap_dev.iface               = std::move(iface);
	clap_dev.name                = clap_dev.iface->plugin.plugin->desc->name;
	clap_dev.ext.data            = std::move(ext_data);
	clap_dev                     = init_gui(std::move(clap_dev));
	clap_dev                     = init_audio(std::move(clap_dev), dev);
	clap_dev                     = init_params(std::move(clap_dev));
	dev.ext.shm_param_info       = make_shm_param_info(app->instance_id, app->options.sbox_id, dev_id, app->uid++, clap_dev.params.size());
	for (size_t i = 0; i < clap_dev.params.size(); i++) {
		const auto& param = clap_dev.params[i];
		shm::param_info info;
		info.id            = std::format("clap/{}", param.info.id);
		info.default_value = param.info.default_value;
		info.max_value     = param.info.max_value;
		info.min_value     = param.info.min_value;
		info.name          = param.info.name;
		info.clap.id       = param.info.id;
		info.clap.cookie   = param.info.cookie;
		dev.ext.shm_param_info->arr[i] = std::move(info);
	}
	const auto m                 = app->model.lock_write();
	m->devices                   = m->devices.insert(dev);
	m->clap_devices              = m->clap_devices.insert(clap_dev);
	app->msg_sender.enqueue(scuff::msg::out::return_created_device{dev.id.value, dev.ext.shm_audio_ports->id().data(), dev.ext.shm_param_info->id().data(), callback});
}

[[nodiscard]] static
auto get_param_value(const sbox::app& app, id::device dev_id, scuff_param param_idx) -> std::optional<double> {
	const auto dev = app.model.lock_read().clap_devices.at(dev_id);
	if (dev.iface->plugin.params) {
		const auto param = dev.params[param_idx];
		double value;
		if (dev.iface->plugin.params->get_value(dev.iface->plugin.plugin, param.info.id, &value)) {
			return value;
		}
	}
	return std::nullopt;
}

[[nodiscard]] static
auto get_param_value_text(const sbox::app& app, id::device dev_id, scuff_param param_idx, double value) -> std::string {
	static constexpr auto BUFFER_SIZE = 50;
	const auto dev = app.model.lock_read().clap_devices.at(dev_id);
	if (dev.iface->plugin.params) {
		const auto param = dev.params[param_idx];
		char buffer[BUFFER_SIZE];
		if (!dev.iface->plugin.params->value_to_text(dev.iface->plugin.plugin, param.info.id, value, buffer, BUFFER_SIZE)) {
			return std::to_string(value);
		}
		return buffer;
	}
	return std::to_string(value);
}

[[nodiscard]] static
auto load(sbox::app* app, id::device dev_id, const std::vector<std::byte>& state) -> bool {
	const auto dev = app->model.lock_read().clap_devices.at(dev_id);
	if (dev.iface->plugin.state) {
		auto bytes = std::span{state};
		clap_istream_t is;
		is.ctx = (void*)(&bytes);
		is.read = [](const clap_istream_t *stream, void *buffer, uint64_t size) -> int64_t {
			const auto bytes = reinterpret_cast<std::span<std::byte>*>(stream->ctx);
			auto clap_bytes  = static_cast<std::byte*>(buffer);
			const auto read_size = std::min(size, static_cast<uint64_t>(bytes->size()));
			std::copy(bytes->data(), bytes->data() + read_size, clap_bytes);
			*bytes = bytes->subspan(read_size);
			return read_size;
		};
		return dev.iface->plugin.state->load(dev.iface->plugin.plugin, &is);
	}
	return false;
}

[[nodiscard]] static
auto save(sbox::app* app, id::device dev_id) -> std::vector<std::byte> {
	const auto dev = app->model.lock_read().clap_devices.at(dev_id);
	if (dev.iface->plugin.state) {
		std::vector<std::byte> bytes;
		clap_ostream_t os;
		os.ctx = &bytes;
		os.write = [](const clap_ostream_t *stream, const void *buffer, uint64_t size) -> int64_t {
			auto &bytes = *static_cast<std::vector<std::byte>*>(stream->ctx);
			const auto clap_bytes = static_cast<const std::byte*>(buffer);
			std::copy(clap_bytes, clap_bytes + size, std::back_inserter(bytes));
			return size;
		};
		if (!dev.iface->plugin.state->save(dev.iface->plugin.plugin, &os)) {
			return {};
		}
		return bytes;
	}
	return {};
}

[[nodiscard]] static
auto set_sample_rate(const sbox::app& app, id::device dev_id, double sr) -> bool {
	const auto m   = app.model.lock_read();
	const auto dev = m.clap_devices.at(dev_id);
	dev.iface->plugin.plugin->deactivate(dev.iface->plugin.plugin);
	return dev.iface->plugin.plugin->activate(dev.iface->plugin.plugin, sr, SCUFF_VECTOR_SIZE, SCUFF_VECTOR_SIZE);
}

} // scuff::sbox::clap::main