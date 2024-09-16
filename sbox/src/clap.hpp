#pragma once

#include "data.hpp"
#include "common/shm.hpp"
#include <optional>

namespace scuff::sbox::clap {

namespace main {

[[nodiscard]] static
auto is_active(const device& device) -> bool {
	const auto& flags = device.ext.data->atomic_flags;
	return flags.value.load(std::memory_order_relaxed) & device_atomic_flags::active;
}

[[nodiscard]] static
auto is_flag_set(uint32_t flags, uint32_t flag) -> bool {
	return (flags & flag) == flag;
}

static
auto make_audio_buffers(const std::vector<clap_audio_port_info_t>& port_info, audio_buffers_detail* out) -> void {
	out->arrays.resize(port_info.size());
	out->buffers.resize(port_info.size());
	size_t total_vectors = 0;
	for (size_t port_index = 0; port_index < port_info.size(); port_index++) {
		const auto& info = port_info[port_index];
		auto& arr = out->arrays[port_index];
		arr.resize(info.channel_count);
		total_vectors += info.channel_count;
	}
	out->vectors.resize(total_vectors);
	size_t vector_index = 0;
	for (size_t port_index = 0; port_index < port_info.size(); port_index++) {
		const auto& info = port_info[port_index];
		auto& arr = out->arrays[port_index];
		auto& buf = out->buffers[port_index];
		for (uint32_t c = 0; c < info.channel_count; c++) {
			auto& vec = out->vectors[vector_index++];
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
auto make_audio_buffers(const audio_port_info& port_info, clap::audio_buffers* out) -> void {
	*out = {};
	make_audio_buffers(port_info.inputs, &out->inputs);
	make_audio_buffers(port_info.outputs, &out->outputs);
}

[[nodiscard]] static
auto retrieve_audio_port_info(const iface_plugin& iface) -> audio_port_info {
	auto out = audio_port_info{};
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
	return out;
}

[[nodiscard]] static
auto make_input_event_list(device_ext_audio* audio) -> clap_input_events_t {
	clap_input_events_t list;
	list.ctx = &audio->input_event_buffer;
	list.size = [](const clap_input_events_t* list) -> uint32_t {
		const auto& event_buffer = *static_cast<const clap::event_buffer*>(list->ctx);
		return static_cast<uint32_t>(event_buffer.size());
	};
	list.get = [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
		const auto& event_buffer = *static_cast<const clap::event_buffer*>(list->ctx);
		return &scuff::clap::convert(event_buffer[index]);
	};
	return list;
}

[[nodiscard]] static
auto make_output_event_list(device_ext_audio* audio) -> clap_output_events_t {
	clap_output_events_t list;
	list.ctx = &audio->output_event_buffer;
	list.try_push = [](const clap_output_events_t* list, const clap_event_header_t* event) -> bool {
		const auto event_buffer = static_cast<clap::event_buffer*>(list->ctx);
		if (const auto converted_event = scuff::clap::convert(*event)) {
			event_buffer->push_back(*converted_event);
		}
		return true;
	};
	return list;
}

static
// AUDIO DEVICE
auto initialize_process_struct_for_audio_device(clap::device_ext_audio* audio) -> void {
	audio->input_events                = make_input_event_list(audio);
	audio->output_events               = make_output_event_list(audio);
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
auto initialize_process_struct_for_event_device(clap::device_ext_audio* audio) -> void {
	audio->input_events                = make_input_event_list(audio);
	audio->output_events               = make_output_event_list(audio);
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
auto init_audio(device dev) -> std::shared_ptr<const device_ext_audio> {
	auto out = device_ext_audio{};
	if (dev.iface_plugin.audio_ports) {
		// AUDIO PLUGIN
		out.port_info = retrieve_audio_port_info(dev.iface_plugin);
		make_audio_buffers(out.port_info, &out.buffers);
		initialize_process_struct_for_audio_device(&out);
	}
	else {
		// EVENT-ONLY PLUGIN
		initialize_process_struct_for_event_device(&out);
	}
	return std::make_shared<const device_ext_audio>(std::move(out));
}

static
auto init_audio(sbox::app* app, id::device dev_id) -> void {
	const auto m    = app->working_model.lock();
	auto dev        = m->clap_devices.at(dev_id);
	dev.ext.audio   = init_audio(dev);
	m->clap_devices = m->clap_devices.insert(dev);
	app->published_model.set(*m);
}

static
auto rescan_audio_ports(sbox::app* app, id::device dev_id, uint32_t flags) -> void {
	const auto requires_not_active =
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_CHANNEL_COUNT) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_FLAGS) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_PORT_TYPE) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_IN_PLACE_PAIR) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_LIST);
	const auto m   = *app->working_model.lock();
	const auto dev = m.clap_devices.at(dev_id);
	if (requires_not_active) {
		if (is_active(dev)) {
			app->msg_sender.enqueue(scuff::msg::out::report_warning{std::format("Device '{}' tried to rescan audio ports while it is still active!", *dev.name)});
			return;
		}
	}
	init_audio(app, dev_id);
}

} // main

static
auto send_msg(sbox::app* app, id::device dev_id, const clap::device_msg::msg& msg) -> void {
	// TODO:
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
	const auto m   = *app->working_model.lock();
	const auto dev = m.clap_devices.at(dev_id);
	if (extension_id == std::string_view{CLAP_EXT_AUDIO_PORTS})         { return &dev.iface_host.audio_ports; }
	if (extension_id == std::string_view{CLAP_EXT_CONTEXT_MENU})        { return nullptr; } // Not implemented yet &iface.context_menu; }
	if (extension_id == std::string_view{CLAP_EXT_CONTEXT_MENU_COMPAT}) { return nullptr; } // Not implemented yet &iface.context_menu; }
	if (extension_id == std::string_view{CLAP_EXT_GUI})                 { return &dev.iface_host.gui; }
	if (extension_id == std::string_view{CLAP_EXT_LATENCY})             { return &dev.iface_host.latency; }
	if (extension_id == std::string_view{CLAP_EXT_LOG})                 { return &dev.iface_host.log; }
	if (extension_id == std::string_view{CLAP_EXT_PARAMS})              { return &dev.iface_host.params; }
	if (extension_id == std::string_view{CLAP_EXT_PRESET_LOAD})         { return nullptr; } // Not implemented yet &iface.preset_load; }
	if (extension_id == std::string_view{CLAP_EXT_PRESET_LOAD_COMPAT})  { return nullptr; } // Not implemented yet &iface.preset_load; }
	if (extension_id == std::string_view{CLAP_EXT_STATE})               { return &dev.iface_host.state; }
	if (extension_id == std::string_view{CLAP_EXT_THREAD_CHECK})        { return &dev.iface_host.thread_check; }
	if (extension_id == std::string_view{CLAP_EXT_TRACK_INFO})          { return &dev.iface_host.track_info; }
	if (extension_id == std::string_view{CLAP_EXT_TRACK_INFO_COMPAT})   { return &dev.iface_host.track_info; }
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
	const auto ext = app->working_model.lock()->clap_devices.at(dev_id).ext;
	ext.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_param_flush, std::memory_order_relaxed);
}

static
auto cb_request_process(sbox::app* app, id::device dev_id) -> void {
	const auto ext = app->working_model.lock()->clap_devices.at(dev_id).ext;
	ext.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_active | device_atomic_flags::schedule_process);
}

static
auto cb_request_restart(sbox::app* app, id::device dev_id) -> void {
	const auto ext = app->working_model.lock()->clap_devices.at(dev_id).ext;
	ext.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_restart);
}

static
auto cb_request_callback(sbox::app* app, id::device dev_id) -> void {
	const auto ext = app->working_model.lock()->clap_devices.at(dev_id).ext;
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
		// TODO: Figure out what we do here
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
		// TODO: sort this out
		return true;
	};
	return iface;
}

[[nodiscard]] static
auto create_device(sbox::model m, sbox::app* app, id::device dev_id, std::string_view plugfile_path, std::string_view plugin_id, size_t callback) -> sbox::model {
	auto dev      = scuff::sbox::device{};
	auto clap_dev = scuff::sbox::clap::device{};
	dev.id      = dev_id;
	clap_dev.id = dev_id;
	dev.type    = scuff_plugin_type::clap;
	device_external ext;
	ext.shm_device      = std::make_shared<shm::device>(bip::open_only, shm::segment::remove_when_done, shm::device::make_id(app->instance_id, dev_id));
	ext.shm_audio_ports = std::make_shared<shm::device_audio_ports>(bip::open_only, shm::segment::remove_when_done, shm::device_audio_ports::make_id(app->instance_id, app->options.sbox_id, dev_id, app->uid++));
	ext.shm_param_info  = std::make_shared<shm::device_param_info>(bip::open_only, shm::segment::remove_when_done, shm::device_param_info::make_id(app->instance_id, app->options.sbox_id, dev_id, app->uid++));
	dev.ext = std::move(ext);
	// TODO: the rest of this
	app->msg_sender.enqueue(scuff::msg::out::return_created_device{dev_id.value, dev.ext->shm_audio_ports->id().data(), dev.ext->shm_param_info->id().data(), callback});
	return m;
}

[[nodiscard]] static
auto get_param_value(const sbox::app& app, id::device dev_id, scuff_param param_idx) -> std::optional<double> {
	const auto dev = app.working_model.lock()->clap_devices.at(dev_id);
	if (dev.iface_plugin.params) {
		const auto param = dev.params[param_idx];
		double value;
		if (dev.iface_plugin.params->get_value(dev.iface_plugin.plugin, param.id, &value)) {
			return value;
		}
	}
	return std::nullopt;
}

[[nodiscard]] static
auto get_param_value_text(const sbox::app& app, id::device dev_id, scuff_param param_idx, double value) -> std::string {
	static constexpr auto BUFFER_SIZE = 50;
	const auto dev = app.working_model.lock()->clap_devices.at(dev_id);
	if (dev.iface_plugin.params) {
		const auto param = dev.params[param_idx];
		char buffer[BUFFER_SIZE];
		if (!dev.iface_plugin.params->value_to_text(dev.iface_plugin.plugin, param.id, value, buffer, BUFFER_SIZE)) {
			return std::to_string(value);
		}
		return buffer;
	}
	return std::to_string(value);
}

[[nodiscard]] static
auto load(sbox::app* app, id::device dev_id, const std::vector<std::byte>& state) -> bool {
	const auto dev = app->working_model.lock()->clap_devices.at(dev_id);
	if (dev.iface_plugin.state) {
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
		return dev.iface_plugin.state->load(dev.iface_plugin.plugin, &is);
	}
	return false;
}

[[nodiscard]] static
auto save(sbox::app* app, id::device dev_id) -> std::vector<std::byte> {
	const auto dev = app->working_model.lock()->clap_devices.at(dev_id);
	if (dev.iface_plugin.state) {
		std::vector<std::byte> bytes;
		clap_ostream_t os;
		os.ctx = &bytes;
		os.write = [](const clap_ostream_t *stream, const void *buffer, uint64_t size) -> int64_t {
			auto &bytes = *static_cast<std::vector<std::byte>*>(stream->ctx);
			const auto clap_bytes = static_cast<const std::byte*>(buffer);
			std::copy(clap_bytes, clap_bytes + size, std::back_inserter(bytes));
			return size;
		};
		if (!dev.iface_plugin.state->save(dev.iface_plugin.plugin, &os)) {
			return {};
		}
		return bytes;
	}
	return {};
}

[[nodiscard]] static
auto set_sample_rate(const sbox::app& app, id::device dev_id, double sr) -> bool {
	const auto m   = *app.working_model.lock();
	const auto dev = m.clap_devices.at(dev_id);
	dev.iface_plugin.plugin->deactivate(dev.iface_plugin.plugin);
	return dev.iface_plugin.plugin->activate(dev.iface_plugin.plugin, sr, SCUFF_VECTOR_SIZE, SCUFF_VECTOR_SIZE);
}

} // scuff::sbox::clap