#pragma once

#include "data.hpp"
#include "common-clap.hpp"
#include "common-messages.hpp"
#include "common-shm.hpp"
#include "common-visit.hpp"
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
	return is_flag_set(device.flags.value, device_flags::active);
}

[[nodiscard]] static
auto is_processing(const device& device) -> bool {
	return is_flag_set(device.service.data->atomic_flags, device_atomic_flags::processing);
}

[[nodiscard]] static
auto is_scheduled_to_process(const device& device) -> bool {
	return is_flag_set(device.service.data->atomic_flags, device_atomic_flags::schedule_process);
}

static
auto send_msg(const device& dev, const clap::device_msg::msg& msg) -> void {
	dev.service.data->msg_q.enqueue(msg);
}

static
auto send_msg(sbox::app* app, id::device dev_id, const clap::device_msg::msg& msg) -> void {
	const auto m = app->model.rt_read();
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
		const auto count = std::min(chunk_size, len - i);
		const auto chunk = std::string_view{msg + i, count};
		send_msg(app, dev_id, device_msg::log_text{{chunk.data(), count}});
	}
	send_msg(app, dev_id, device_msg::log_end{});
}

[[nodiscard]] static
auto get_extension(sbox::app* app, const clap::iface_host& iface_host, id::device dev_id, std::string_view extension_id) -> const void* {
	const auto m   = app->model.rt_read();
	if (extension_id == std::string_view{CLAP_EXT_AUDIO_PORTS})         { return &iface_host.audio_ports; }
	if (extension_id == std::string_view{CLAP_EXT_CONTEXT_MENU})        { return nullptr; } // Not implemented yet &iface.context_menu; }
	if (extension_id == std::string_view{CLAP_EXT_CONTEXT_MENU_COMPAT}) { return nullptr; } // Not implemented yet &iface.context_menu; }
	if (extension_id == std::string_view{CLAP_EXT_GUI})                 { return &iface_host.gui; }
	if (extension_id == std::string_view{CLAP_EXT_LATENCY})             { return &iface_host.latency; }
	if (extension_id == std::string_view{CLAP_EXT_LOG})                 { return &iface_host.log; }
	if (extension_id == std::string_view{CLAP_EXT_PARAMS})              { return &iface_host.params; }
	if (extension_id == std::string_view{CLAP_EXT_PRESET_LOAD})         { return nullptr; } // Not implemented yet &iface.preset_load; }
	if (extension_id == std::string_view{CLAP_EXT_PRESET_LOAD_COMPAT})  { return nullptr; } // Not implemented yet &iface.preset_load; }
	if (extension_id == std::string_view{CLAP_EXT_STATE})               { return &iface_host.state; }
	if (extension_id == std::string_view{CLAP_EXT_THREAD_CHECK})        { return &iface_host.thread_check; }
	if (extension_id == std::string_view{CLAP_EXT_TRACK_INFO})          { return &iface_host.track_info; }
	if (extension_id == std::string_view{CLAP_EXT_TRACK_INFO_COMPAT})   { return &iface_host.track_info; }
	if (extension_id == std::string_view{CLAP_EXT_TAIL}) {
		// Not supported at the moment because this extension is too under-specified
		// https://github.com/free-audio/clap/blob/main/include/clap/ext/tail.h
		return nullptr;
	}
	app->msg_sender.enqueue(scuff::msg::out::report_warning{std::format("Device '{}' requested an unsupported extension: {}", dev_id.value, extension_id)});
	return nullptr;
}

[[nodiscard]] static
auto get_host_data(const clap_host_t* host) -> device_host_data& {
	return *static_cast<device_host_data*>(host->host_data);
}

static
auto cb_params_rescan(sbox::app* app, id::device dev_id, clap_param_rescan_flags flags) -> void {
	send_msg(app, dev_id, device_msg::params_rescan{flags});
}

static
auto cb_request_param_flush(sbox::app* app, id::device dev_id) -> void {
	const auto svc = app->model.rt_read()->clap_devices.at(dev_id).service;
	svc.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_param_flush, std::memory_order_relaxed);
}

static
auto cb_request_process(sbox::app* app, id::device dev_id) -> void {
	const auto svc = app->model.rt_read()->clap_devices.at(dev_id).service;
	svc.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_active | device_atomic_flags::schedule_process);
}

static
auto cb_request_restart(sbox::app* app, id::device dev_id) -> void {
	const auto svc = app->model.rt_read()->clap_devices.at(dev_id).service;
	svc.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_restart);
}

static
auto cb_request_callback(sbox::app* app, id::device dev_id) -> void {
	const auto svc = app->model.rt_read()->clap_devices.at(dev_id).service;
	svc.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_callback);
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
	send_msg(app, dev_id, device_msg::gui_request_resize{{width, height}});
}

static
auto cb_gui_resize_hints_changed(sbox::app* app, id::device dev_id) -> void {
	send_msg(app, dev_id, device_msg::gui_resize_hints_changed{});
}

static
auto convert_input_events(const sbox::device& dev, const clap::device& clap_dev) -> void {
	auto get_cookie = [dev](idx::param param) -> void* {
		return dev.service->shm.data->param_info[param.value].clap.cookie;
	};
	auto get_id = [dev](idx::param param) -> clap_id {
		return dev.service->shm.data->param_info[param.value].id.value;
	};
	auto fns = scuff::events::clap::scuff_to_clap_conversion_fns{get_cookie, get_id};
	scuff::events::clap::event_buffer input_clap_events;
	for (const auto& event : dev.service->shm.data->events_in) {
		input_clap_events.push_back(scuff::events::clap::from_scuff(event, fns));
	}
	clap_dev.service.data->input_event_buffer = std::move(input_clap_events);
	dev.service->shm.data->events_in.clear();
}

static
auto convert_output_events(const sbox::device& dev, const clap::device& clap_dev) -> void {
	auto find_param = [dev](clap_id id) -> idx::param {
		auto has_id = [id](const scuff::param_info& info) -> bool {
			return info.id.value == id;
		};
		const auto& infos = dev.service->shm.data->param_info;
		const auto pos    = std::find_if(std::begin(infos), std::end(infos), has_id);
		if (pos == std::end(infos)) {
			throw std::runtime_error(std::format("Could not find parameter with CLAP id: {}", id));
		}
		return static_cast<idx::param>(std::distance(std::begin(infos), pos));
	};
	auto fns = scuff::events::clap::clap_to_scuff_conversion_fns{find_param};
	scuff::event_buffer output_scuff_events;
	for (const auto& event : clap_dev.service.data->output_event_buffer) {
		output_scuff_events.push_back(scuff::events::clap::to_scuff(event, fns));
	}
	dev.service->shm.data->events_out = std::move(output_scuff_events);
	clap_dev.service.data->output_event_buffer.clear();
}

static
// Could be called from main thread or audio thread, but
// never both simultaneously, for the same device.
auto flush_device_events(const sbox::device& dev, const clap::device& clap_dev) -> void {
	const auto& input_events  = clap_dev.service.audio->input_events;
	const auto& output_events = clap_dev.service.audio->output_events;
	const auto& iface         = clap_dev.iface->plugin;
	if (!iface.params) {
		// May not actually be intialized
		return;
	}
	convert_input_events(dev, clap_dev);
	iface.params->flush(iface.plugin, &input_events, &output_events);
	convert_output_events(dev, clap_dev);
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
	unset_flags(&dev.service.data->atomic_flags, device_atomic_flags::schedule_process);
	if (!dev.iface->plugin.plugin->start_processing(dev.iface->plugin.plugin)) {
		return false;
	}
	set_flags(&dev.service.data->atomic_flags, device_atomic_flags::processing);
	return true;
}

[[nodiscard]] static
auto output_is_quiet(const shm::device& shm) -> bool {
	static constexpr auto THRESHOLD = 0.0001f;
	for (size_t i = 0; i < shm.data->audio_out.size(); i++) {
		const auto& buffer = shm.data->audio_out[i];
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
	unset_flags(&dev.service.data->atomic_flags, device_atomic_flags::processing);
}

[[nodiscard]] static
auto handle_audio_process_result(const shm::device& shm, const clap::device& dev, clap_process_status status) -> void {
	switch (status) {
		case CLAP_PROCESS_CONTINUE: {
			return;
		}
		case CLAP_PROCESS_CONTINUE_IF_NOT_QUIET: {
			if (output_is_quiet(shm)) {
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
	const auto& process = clap_dev.service.audio->process;
	auto& flags         = clap_dev.service.data->atomic_flags;
	auto& audio_buffers = clap_dev.service.audio->buffers;
	convert_input_events(dev, clap_dev);
	const auto status = iface.plugin->process(iface.plugin, &process);
	handle_audio_process_result(dev.service->shm, clap_dev, status);
	convert_output_events(dev, clap_dev);
}

static
auto process_event_device(const sbox::device& dev, const clap::device& clap_dev) -> void {
	const auto& iface   = clap_dev.iface->plugin;
	const auto& process = clap_dev.service.audio->process;
	auto& flags         = clap_dev.service.data->atomic_flags;
	convert_input_events(dev, clap_dev);
	const auto status   = iface.plugin->process(iface.plugin, &process);
	handle_event_process_result(clap_dev, status);
	convert_output_events(dev, clap_dev);
}

auto process(const sbox::app& app, const sbox::device& dev) -> void {
	const auto& clap_dev = app.audio_model->clap_devices.at(dev.id);
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
		if (can_render_audio(clap_dev.service.audio->buffers)) {
			process_audio_device(dev, clap_dev);
			return;
		}
	}
	process_event_device(dev, clap_dev);
}

} // namespace scuff::sbox::clap::audio

namespace scuff::sbox::clap::main {

static
auto make_audio_buffers(bc::static_vector<shm::audio_buffer, MAX_AUDIO_PORTS>* shm_buffers, const std::vector<clap_audio_port_info_t>& port_info, audio_buffers_detail* out) -> void {
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
			auto& vec = (*shm_buffers)[port_index];
			arr[c] = vec.data() + (scuff::VECTOR_SIZE * c);
		}
		buf.channel_count = info.channel_count;
		buf.constant_mask = 0;
		buf.data32        = arr.data();
		buf.data64        = nullptr;
		buf.latency       = 0;
	}
}

static
auto make_audio_buffers(const shm::device& shm, const audio_port_info& port_info, clap::audio_buffers* out) -> void {
	*out = {};
	make_audio_buffers(&shm.data->audio_in, port_info.inputs, &out->inputs);
	make_audio_buffers(&shm.data->audio_out, port_info.outputs, &out->outputs);
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
auto make_input_event_list(const clap::device& dev) -> clap_input_events_t {
	clap_input_events_t list;
	list.ctx = &dev.service.data->input_events_context;
	list.size = [](const clap_input_events_t* list) -> uint32_t {
		const auto& ctx = *static_cast<const clap::event_queue_context*>(list->ctx);
		return static_cast<uint32_t>(ctx.service_data->input_event_buffer.size());
	};
	list.get = [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
		const auto& ctx = *static_cast<const clap::event_queue_context*>(list->ctx);
		return &scuff::events::clap::to_header(ctx.service_data->input_event_buffer[index]);
	};
	return list;
}

[[nodiscard]] static
auto make_output_event_list(const clap::device& dev) -> clap_output_events_t {
	clap_output_events_t list;
	list.ctx = &dev.service.data->output_events_context;
	list.try_push = [](const clap_output_events_t* list, const clap_event_header_t* hdr) -> bool {
		const auto ctx = static_cast<clap::event_queue_context*>(list->ctx);
		ctx->service_data->output_event_buffer.push_back(scuff::events::clap::to_event(*hdr));
		return true;
	};
	return list;
}

static
// AUDIO DEVICE
auto initialize_process_struct_for_audio_device(const clap::device& dev, clap::device_service_audio* audio) -> void {
	audio->input_events                = make_input_event_list(dev);
	audio->output_events               = make_output_event_list(dev);
	audio->process.frames_count        = VECTOR_SIZE;
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
auto initialize_process_struct_for_event_device(const clap::device& dev, clap::device_service_audio* audio) -> void {
	audio->input_events                = make_input_event_list(dev);
	audio->output_events               = make_output_event_list(dev);
	static auto dummy_buffer           = clap_audio_buffer_t{0};
	audio->process.frames_count        = VECTOR_SIZE;
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
auto init_audio(const sbox::device& dev, const clap::device& clap_dev) -> std::shared_ptr<const device_service_audio> {
	auto out = device_service_audio{};
	if (clap_dev.iface->plugin.audio_ports) {
		// AUDIO PLUGIN
		make_audio_buffers(dev.service->shm, clap_dev.service.audio_port_info, &out.buffers);
		initialize_process_struct_for_audio_device(clap_dev, &out);
	}
	else {
		// EVENT-ONLY PLUGIN
		initialize_process_struct_for_event_device(clap_dev, &out);
	}
	return std::make_shared<const device_service_audio>(std::move(out));
}

[[nodiscard]] static
auto init_audio(clap::device&& clap_dev, const sbox::device& dev) -> device {
	clap_dev.service.audio = init_audio(dev, clap_dev);
	return clap_dev;
}

static
auto init_audio(sbox::app* app, id::device dev_id) -> void {
	app->model.update_publish([=](model&& m) {
		auto dev                         = m.devices.at(dev_id);
		auto clap_dev                    = m.clap_devices.at(dev_id);
		clap_dev.service.audio_port_info = retrieve_audio_port_info(clap_dev.iface->plugin);
		clap_dev                         = init_audio(std::move(clap_dev), dev);
		m.clap_devices                  = m.clap_devices.insert(clap_dev);
		return m;
	});
}

static
auto rescan_audio_ports(sbox::app* app, id::device dev_id, uint32_t flags) -> void {
	const auto requires_not_active =
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_CHANNEL_COUNT) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_FLAGS) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_PORT_TYPE) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_IN_PLACE_PAIR) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_LIST);
	const auto m   = app->model.read();
	const auto dev = m.clap_devices.at(dev_id);
	if (requires_not_active) {
		if (is_active(dev)) {
			app->msg_sender.enqueue(scuff::msg::out::report_warning{std::format("Device '{}' tried to rescan audio ports while it is still active!", *dev.name)});
			return;
		}
	}
	init_audio(app, dev_id);
}

[[nodiscard]] static
auto init_params(sbox::device&& dev, const clap::device& clap_dev) -> sbox::device {
	const auto& iface = clap_dev.iface->plugin;
	if (iface.params) {
		dev.service->shm.data->flags.value |= shm::device_flags::has_params;
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
	}
	return dev;
}

static
auto init_params_shm(const sbox::device& dev, const clap::device& clap_dev) -> void {
	auto lock = std::lock_guard{dev.service->shm.data->param_info_mutex};
	dev.service->shm.data->param_info.resize(clap_dev.params.size());
	for (size_t i = 0; i < clap_dev.params.size(); i++) {
		const auto& param = clap_dev.params[i];
		scuff::param_info info;
		info.id            = {param.info.id};
		info.default_value = param.info.default_value;
		info.max_value     = param.info.max_value;
		info.min_value     = param.info.min_value;
		info.clap.cookie   = param.info.cookie;
		const auto name_buffer_size = std::min(std::size(info.name), std::size(param.info.name));
		std::copy_n(std::begin(param.info.name), name_buffer_size, std::begin(info.name));
		info.name[name_buffer_size - 1] = '\0';
		dev.service->shm.data->param_info[i] = std::move(info);
	}
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_closed& msg) -> void {
	// TOODOO: process msg
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_request_hide& msg) -> void {
	// TOODOO: process msg
}

static
auto process_msg_(sbox::app* app, const device& clap_dev, const clap::device_msg::gui_request_resize& msg) -> void {
	const auto& dev = app->model.read().devices.at(clap_dev.id);
	dev.service->scheduled_window_resize = msg.size;
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_request_show& msg) -> void {
	// TOODOO: process msg
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::gui_resize_hints_changed& msg) -> void {
	// TOODOO: process msg
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::log_begin& msg) -> void {
	dev.service.data->log_collector.severity = msg.severity;
	dev.service.data->log_collector.chunks.clear();
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::log_end& msg) -> void {
	std::string text;
	for (const auto& chunk : dev.service.data->log_collector.chunks) {
		text.append(chunk.c_str());
	}
	switch (*dev.service.data->log_collector.severity) {
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
	dev.service.data->log_collector.severity = std::nullopt;
	dev.service.data->log_collector.chunks.clear();
}

static
auto process_msg_(sbox::app* app, const device& dev, const clap::device_msg::log_text& msg) -> void {
	dev.service.data->log_collector.chunks.push_back(msg.text);
}

static
auto process_msg_(sbox::app* app, clap::device clap_dev, const clap::device_msg::params_rescan& msg) -> void {
	const auto m    = app->model.read();
	const auto& dev = m.devices.at(clap_dev.id);
	clap_dev = init_params(std::move(clap_dev));
	init_params_shm(dev, clap_dev);
	app->model.update_publish([clap_dev](model&& m){
		m.clap_devices = m.clap_devices.insert(clap_dev);
		return m;
	});
	app->msg_sender.enqueue(scuff::msg::out::device_params_changed{dev.id.value});
}

static
auto process_msg(sbox::app* app, const device& dev, const clap::device_msg::msg& msg) -> void {
	fast_visit([app, dev](const auto& msg) { process_msg_(app, dev, msg); }, msg);
}

static
auto update(sbox::app* app) -> void {
	const auto m = app->model.read();
	for (const auto& dev : m.clap_devices) {
		clap::device_msg::msg msg;
		while (dev.service.data->msg_q.try_dequeue(msg)) {
			process_msg(app, dev, msg);
		}
	}
}

} // namespace scuff::sbox::clap::main

namespace scuff::sbox::clap::main {

static
auto make_host_for_instance(device_host_data* host_data) -> void {
	host_data->iface.host.clap_version = CLAP_VERSION;
	host_data->iface.host.name         = "scuff-sbox";
	host_data->iface.host.url          = "https://github.com/colugomusic/scuff";
	host_data->iface.host.vendor       = "Moron Enterprises";
	host_data->iface.host.host_data    = host_data;
	host_data->iface.host.get_extension = [](const clap_host_t* host, const char* extension_id) -> const void* {
		const auto& hd = get_host_data(host);
		return get_extension(hd.app, hd.iface, hd.dev_id, extension_id);
	};
	host_data->iface.host.request_callback = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_callback(hd.app, hd.dev_id);
	};
	host_data->iface.host.request_process = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_process(hd.app, hd.dev_id);
	};
	host_data->iface.host.request_restart = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_restart(hd.app, hd.dev_id);
	};
	// AUDIO PORTS ______________________________________________________________
	host_data->iface.audio_ports.is_rescan_flag_supported = [](const clap_host* host, uint32_t flag) -> bool {
		return true;
	};
	host_data->iface.audio_ports.rescan = [](const clap_host* host, uint32_t flags) -> void {
		const auto& hd = get_host_data(host);
		main::rescan_audio_ports(hd.app, hd.dev_id, flags);
	};
	// CONTEXT MENU _____________________________________________________________
	host_data->iface.context_menu.can_popup = [](const clap_host* host) -> bool {
		// Not implemented yet
		return false;
	};
	host_data->iface.context_menu.perform = [](const clap_host* host, const clap_context_menu_target_t* target, clap_id action_id) -> bool {
		// Not implemented yet
		return false;
	};
	host_data->iface.context_menu.populate = [](const clap_host* host, const clap_context_menu_target_t* target, const clap_context_menu_builder_t* builder) -> bool {
		// Not implemented yet
		return false;
	};
	host_data->iface.context_menu.popup = [](const clap_host* host, const clap_context_menu_target_t* target, int32_t screen_index, int32_t x, int32_t y) -> bool {
		// Not implemented yet
		return false;
	};
	// GUI ______________________________________________________________________
	host_data->iface.gui.closed = [](const clap_host* host, bool was_destroyed) -> void {
		const auto& hd = get_host_data(host);
		cb_gui_closed(hd.app, hd.dev_id, was_destroyed);
	};
	host_data->iface.gui.request_hide = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_hide(hd.app, hd.dev_id);
		return true;
	};
	host_data->iface.gui.request_show = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_show(hd.app, hd.dev_id);
		return true;
	};
	host_data->iface.gui.request_resize = [](const clap_host* host, uint32_t width, uint32_t height) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_resize(hd.app, hd.dev_id, width, height);
		return true;
	};
	host_data->iface.gui.resize_hints_changed = [](const clap_host* host) -> void {
		const auto& hd = get_host_data(host);
		cb_gui_resize_hints_changed(hd.app, hd.dev_id);
	};
	// LATENCY __________________________________________________________________
	host_data->iface.latency.changed = [](const clap_host* host) -> void {
		// I don't do anything with this yet
	};
	// LOG ----------------------------------------------------------------------
	host_data->iface.log.log = [](const clap_host* host, clap_log_severity severity, const char* msg) -> void {
		const auto& hd = get_host_data(host);
		clap::log(hd.app, hd.dev_id, severity, msg);
	};
	// PARAMS ___________________________________________________________________
	host_data->iface.params.clear = [](const clap_host* host, clap_id param_id, clap_param_clear_flags flags) -> void {
		// I don't do anything with this yet
	};
	host_data->iface.params.request_flush = [](const clap_host* host) -> void {
		const auto& hd = get_host_data(host);
		cb_request_param_flush(hd.app, hd.dev_id);
	};
	host_data->iface.params.rescan = [](const clap_host* host, clap_param_rescan_flags flags) -> void {
		const auto& hd = get_host_data(host);
		cb_params_rescan(hd.app, hd.dev_id, flags);
	};
	// PRESET LOAD ______________________________________________________________
	host_data->iface.preset_load.loaded = [](const clap_host* host, uint32_t location_kind, const char* location, const char* load_key) -> void {
		// Not implemented yet
	};
	host_data->iface.preset_load.on_error = [](const clap_host* host, uint32_t location_kind, const char* location, const char* load_key, int32_t os_error, const char* msg) -> void {
		// Not implemented yet
	};
	// STATE ____________________________________________________________________
	host_data->iface.state.mark_dirty = [](const clap_host* host) -> void {
		// I don't do anything with this yet
	};
	// THREAD CHECK _____________________________________________________________
	host_data->iface.thread_check.is_audio_thread = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		return hd.app->main_thread_id != std::this_thread::get_id();
	};
	host_data->iface.thread_check.is_main_thread = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		return hd.app->main_thread_id == std::this_thread::get_id();
	};
	// TRACK INFO _______________________________________________________________
	host_data->iface.track_info.get = [](const clap_host* host, clap_track_info_t* info) -> bool {
		const auto& hd = get_host_data(host);
		// TOODOO: track_info.get: sort this out
		return true;
	};
}

static
auto get_extensions(clap::iface_plugin* iface) -> void {
	iface->audio_ports  = scuff::get_plugin_ext<clap_plugin_audio_ports_t>(*iface->plugin, CLAP_EXT_AUDIO_PORTS);
	iface->context_menu = scuff::get_plugin_ext<clap_plugin_context_menu_t>(*iface->plugin, CLAP_EXT_CONTEXT_MENU, CLAP_EXT_CONTEXT_MENU_COMPAT);
	iface->gui          = scuff::get_plugin_ext<clap_plugin_gui_t>(*iface->plugin, CLAP_EXT_GUI);
	iface->params       = scuff::get_plugin_ext<clap_plugin_params_t>(*iface->plugin, CLAP_EXT_PARAMS);
	iface->render       = scuff::get_plugin_ext<clap_plugin_render_t>(*iface->plugin, CLAP_EXT_RENDER);
	iface->state        = scuff::get_plugin_ext<clap_plugin_state_t>(*iface->plugin, CLAP_EXT_STATE);
	iface->tail         = scuff::get_plugin_ext<clap_plugin_tail_t>(*iface->plugin, CLAP_EXT_TAIL);
}

[[nodiscard]] static
auto init_gui(sbox::device&& dev, const clap::device& clap_dev) -> sbox::device {
	const auto& iface = clap_dev.iface->plugin;
	if (iface.gui) {
		if (iface.gui->is_api_supported(iface.plugin, scuff::os::get_clap_window_api(), false)) {
			dev.service->shm.data->flags.value |= shm::device_flags::has_gui;
			return dev;
		}
	}
	return dev;
}

[[nodiscard]] static
auto make_ext_data(sbox::app* app, id::device id) -> std::shared_ptr<clap::device_service_data> {
	auto data = std::make_shared<clap::device_service_data>();
	data->host_data.app    = app;
	data->host_data.dev_id = id;
	data->input_events_context.service_data  = data.get();
	data->output_events_context.service_data = data.get();
	return data;
}

[[nodiscard]] static
auto make_shm_device(std::string_view sbox_shmid, id::device dev_id) -> shm::device {
	return shm::open_or_create_device(shm::make_device_id(sbox_shmid, dev_id), false);
}

static
auto create_device(sbox::app* app, id::device dev_id, std::string_view plugfile_path, std::string_view plugin_id, size_t callback) -> void {
	// TOODOO: stop opening dylib multiple times?
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
	auto ext_data = make_ext_data(app, dev_id);
	make_host_for_instance(&ext_data->host_data);
	clap::iface iface;
	iface.plugin.plugin = factory->create_plugin(factory, &ext_data->host_data.iface.host, plugin_id.data());
	if (!iface.plugin.plugin) {
		entry->deinit();
		throw std::runtime_error("clap_plugin_factory.create_plugin failed");
	}
	if (!iface.plugin.plugin->init(iface.plugin.plugin)) {
		throw std::runtime_error("clap_plugin.init failed");
	}
	get_extensions(&iface.plugin);
	auto dev                         = sbox::device{};
	auto clap_dev                    = clap::device{};
	dev.id                           = dev_id;
	dev.type                         = plugin_type::clap;
	dev.service->shm                 = make_shm_device(app->shm_sbox.seg.id, dev_id);
	clap_dev.service.audio_port_info = retrieve_audio_port_info(iface.plugin);
	const auto audio_in_count    = clap_dev.service.audio_port_info->inputs.size();
	const auto audio_out_count   = clap_dev.service.audio_port_info->outputs.size();
	dev.service->shm.data->audio_in.resize(audio_in_count);
	dev.service->shm.data->audio_out.resize(audio_out_count);
	clap_dev.id           = dev_id;
	clap_dev.iface        = std::move(iface);
	clap_dev.name         = clap_dev.iface->plugin.plugin->desc->name;
	clap_dev.service.data = std::move(ext_data);
	dev                   = init_gui(std::move(dev), clap_dev);
	dev                   = init_params(std::move(dev), clap_dev);
	clap_dev              = init_audio(std::move(clap_dev), dev);
	clap_dev              = init_params(std::move(clap_dev));
	init_params_shm(dev, clap_dev);
	app->model.update_publish([=](model&& m) {
		m.devices      = m.devices.insert(dev);
		m.clap_devices = m.clap_devices.insert(clap_dev);
		return m;
	});
}

[[nodiscard]] static
auto get_param_value(const sbox::app& app, id::device dev_id, idx::param param_idx) -> std::optional<double> {
	const auto dev = app.model.read().clap_devices.at(dev_id);
	if (dev.iface->plugin.params) {
		const auto param = dev.params[param_idx.value];
		double value;
		if (dev.iface->plugin.params->get_value(dev.iface->plugin.plugin, param.info.id, &value)) {
			return value;
		}
	}
	return std::nullopt;
}

[[nodiscard]] static
auto get_param_value_text(const sbox::app& app, id::device dev_id, idx::param param_idx, double value) -> std::string {
	static constexpr auto BUFFER_SIZE = 50;
	const auto dev = app.model.read().clap_devices.at(dev_id);
	if (dev.iface->plugin.params) {
		const auto param = dev.params[param_idx.value];
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
	const auto dev = app->model.read().clap_devices.at(dev_id);
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
	const auto dev = app->model.read().clap_devices.at(dev_id);
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
auto activate(sbox::app* app, id::device dev_id, double sr) -> bool {
	const auto m              = app->model.read();
	const auto dev            = m.devices.at(dev_id);
	const auto clap_dev       = m.clap_devices.at(dev_id);
	const auto already_active = clap_dev.flags.value & device_flags::active;
	const auto current_sr     = dev.sample_rate;
	if (already_active && current_sr == sr) {
		return true;
	}
	if (already_active) {
		clap_dev.iface->plugin.plugin->deactivate(clap_dev.iface->plugin.plugin);
	}
	auto result = clap_dev.iface->plugin.plugin->activate(clap_dev.iface->plugin.plugin, sr, VECTOR_SIZE, VECTOR_SIZE);
	if (!result) {
		return false;
	}
	app->model.update_publish([dev_id, sr](model&& m) {
		m.devices = m.devices.update(dev_id, [sr](sbox::device dev) {
			dev.sample_rate = sr;
			return dev;
		});
		m.clap_devices = m.clap_devices.update(dev_id, [](clap::device clap_dev) {
			clap_dev.flags.value |= device_flags::active;
			return clap_dev;
		});
		return m;
	});
	return true;
}

static
auto deactivate(sbox::app* app, id::device dev_id) -> void {
	const auto m         = app->model.read();
	const auto clap_dev  = m.clap_devices.at(dev_id);
	const auto is_active = clap_dev.flags.value & device_flags::active;
	if (!is_active) {
		return;
	}
	app->model.update_publish([dev_id](model&& m) {
		m.clap_devices = m.clap_devices.update(dev_id, [](clap::device clap_dev) {
			clap_dev.flags.value &= ~device_flags::active;
			return clap_dev;
		});
		return m;
	});
	clap_dev.iface->plugin.plugin->deactivate(clap_dev.iface->plugin.plugin);
}

[[nodiscard]] static
auto create_gui(sbox::app* app, const sbox::device& dev) -> sbox::create_gui_result {
	log(app, "clap::main::create_gui");
	const auto m         = app->model.read();
	const auto clap_dev  = m.clap_devices.at(dev.id);
	const auto iface     = clap_dev.iface->plugin;
	if (!iface.gui) {
		log(app, "iface.gui is null?");
		return {};
	}
	if (!iface.gui->create(iface.plugin, scuff::os::get_clap_window_api(), false)) {
		log(app, "iface.gui->create failed");
		return {};
	}
	log(app, "iface.gui->create succeeded");
	uint32_t width = 5000, height = 5000;
	if (!iface.gui->get_size(iface.plugin, &width, &height)) {
		log(app, "iface.gui->get_size failed");
	}
	const auto resizable = iface.gui->can_resize(iface.plugin);
	sbox::create_gui_result result;
	result.success   = true;
	result.resizable = resizable;
	result.width     = width;
	result.height    = height;
	return result;
}

[[nodiscard]] static
auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> bool {
	log(app, "clap::main::setup_editor_window");
	const auto m         = app->model.read();
	const auto clap_dev  = m.clap_devices.at(dev.id);
	const auto iface     = clap_dev.iface->plugin;
	clap_window_t cw;
	cw.api = scuff::os::get_clap_window_api();
	// TOODOO: cross platform stuff
	cw.win32 = view_native(dev.ui.view);
	if (!iface.gui->set_parent(iface.plugin, &cw)) {
		log(app, "iface.gui->set_parent(%d) failed", int64_t(cw.win32));
		return false;
	}
	log(app, "iface.gui->set_parent(%d) succeeded", int64_t(cw.win32));
	// This return value is ignored because some Plugins return false
	// even if the window is shown.
	iface.gui->show(iface.plugin);
	// TOODOO: More to do with resizing and stuff?
	return true;
}

static
auto shutdown_editor_window(sbox::app* app, const sbox::device& dev) -> void {
	const auto m        = app->model.read();
	const auto clap_dev = m.clap_devices.at(dev.id);
	const auto iface     = clap_dev.iface->plugin;
	if (!iface.gui) {
		return;
	}
	iface.gui->hide(iface.plugin);
	iface.gui->destroy(iface.plugin);
}

[[nodiscard]] static
auto get_gui_size(const clap::iface_plugin& iface) -> std::optional<window_size_u32> {
	uint32_t old_width, old_height;
	if (iface.gui->get_size(iface.plugin, &old_width, &old_height)) {
		return window_size_u32{old_width, old_height};
	}
	return std::nullopt;
}

static
auto on_native_window_resize(const sbox::app* app, const sbox::device& dev, window_size_f native_window_size) -> void {
	const auto m                = app->model.read();
	const auto& clap_dev        = m.clap_devices.at(dev.id);
	const auto iface            = clap_dev.iface->plugin;
	const auto old_size         = get_gui_size(iface);
	auto native_window_size_u32 = window_size_u32{native_window_size};
	auto adjusted_size          = native_window_size_u32;
	iface.gui->adjust_size(iface.plugin, &adjusted_size.width, &adjusted_size.height);
	if (adjusted_size == native_window_size_u32) {
		if (!iface.gui->set_size(iface.plugin, adjusted_size.width, adjusted_size.height)) {
			if (old_size) {
				dev.service->scheduled_window_resize = *old_size;
				iface.gui->set_size(iface.plugin, old_size->width, old_size->height);
			}
		}
		return;
	}
	dev.service->scheduled_window_resize = adjusted_size;
	iface.gui->set_size(iface.plugin, adjusted_size.width, adjusted_size.height);
}

} // scuff::sbox::clap::main