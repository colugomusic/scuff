#pragma once

#include "common-clap.hpp"
#include "common-messages.hpp"
#include "common-os-dso.hpp"
#include "common-shm.hpp"
#include "common-visit.hpp"
#include "data.hpp"
#include "os.hpp"
#include <fulog.hpp>
#include <optional>
#include <ranges>

namespace scuff::sbox::op {

auto make_client_param_info(const sbox::device& dev) -> std::vector<client_param_info>;

} // namespace scuff::sbox::op

namespace scuff::sbox::gui {

static auto hide(ez::main_t, sbox::app* app, sbox::device dev) -> void;
static auto show(ez::main_t, sbox::app* app, scuff::id::device dev_id, edwin::fn::on_window_closed on_closed) -> void;

} // namespace scuff::sbox::gui

namespace scuff::sbox::clap {

// FIXME: move these operations into common lib
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
auto is_active(ez::safe_t, const device& device) -> bool {
	return is_flag_set(device.flags.value, device_flags::active);
}

[[nodiscard]] static
auto is_processing(ez::safe_t, const device& device) -> bool {
	return is_flag_set(device.service.data->atomic_flags, device_atomic_flags::processing);
}

[[nodiscard]] static
auto is_scheduled_to_process(ez::safe_t, const device& device) -> bool {
	return is_flag_set(device.service.data->atomic_flags, device_atomic_flags::schedule_process);
}

static
auto send_msg(ez::safe_t, const device& dev, const clap::device_msg::msg& msg) -> void {
	dev.service.data->msg_q.enqueue(msg);
}

static
auto send_msg(ez::safe_t, sbox::app* app, id::device dev_id, const clap::device_msg::msg& msg) -> void {
	const auto m = app->model.read(ez::rt);
	if (const auto dev = m->clap_devices.find(dev_id)) {
		send_msg(ez::safe, *dev, msg);
	}
}

static
auto log(ez::safe_t, sbox::app* app, id::device dev_id, clap_log_severity severity, const char* msg) -> void {
	send_msg(ez::safe, app, dev_id, device_msg::log_begin{severity});
	// Send the message in chunks of 64 characters
	const auto len        = std::strlen(msg);
	const auto chunk_size = device_msg::log_text::MAX;
	for (size_t i = 0; i < len; i += chunk_size) {
		const auto count = std::min(chunk_size, len - i);
		const auto chunk = std::string_view{msg + i, count};
		send_msg(ez::safe, app, dev_id, device_msg::log_text{{chunk.data(), count}});
	}
	send_msg(ez::safe, app, dev_id, device_msg::log_end{});
}

[[nodiscard]] static
auto get_extension(ez::safe_t, sbox::app* app, const clap::iface_host& iface_host, id::device dev_id, std::string_view extension_id) -> const void* {
	const auto m = app->model.read(ez::safe);
	if (extension_id == std::string_view{CLAP_EXT_AUDIO_PORTS})         { return &iface_host.audio_ports; }
	if (extension_id == std::string_view{CLAP_EXT_CONTEXT_MENU})        { return nullptr; } // Not implemented yet &iface.context_menu;
	if (extension_id == std::string_view{CLAP_EXT_CONTEXT_MENU_COMPAT}) { return nullptr; } // Not implemented yet &iface.context_menu;
	if (extension_id == std::string_view{CLAP_EXT_GUI})                 { return &iface_host.gui; }
	if (extension_id == std::string_view{CLAP_EXT_LATENCY})             { return &iface_host.latency; }
	if (extension_id == std::string_view{CLAP_EXT_LOG})                 { return &iface_host.log; }
	if (extension_id == std::string_view{CLAP_EXT_PARAMS})              { return &iface_host.params; }
	if (extension_id == std::string_view{CLAP_EXT_PRESET_LOAD})         { return nullptr; } // Not implemented yet &iface.preset_load;
	if (extension_id == std::string_view{CLAP_EXT_PRESET_LOAD_COMPAT})  { return nullptr; } // Not implemented yet &iface.preset_load;
	if (extension_id == std::string_view{CLAP_EXT_STATE})               { return &iface_host.state; }
	if (extension_id == std::string_view{CLAP_EXT_THREAD_CHECK})        { return &iface_host.thread_check; }
	if (extension_id == std::string_view{CLAP_EXT_TRACK_INFO})          { return &iface_host.track_info; }
	if (extension_id == std::string_view{CLAP_EXT_TRACK_INFO_COMPAT})   { return &iface_host.track_info; }
	if (extension_id == std::string_view{CLAP_EXT_TAIL}) {
		// Not supported at the moment because this extension is too under-specified
		// https://github.com/free-audio/clap/blob/main/include/clap/ext/tail.h
		return nullptr;
	}
	// Doing a non-realtime-safe lock here. If this is actually a problem then the
	// plugin is likely doing something completely insane anyway.
	app->msgs_out.lock()->push_back(msg::out::report_warning{std::format("Device '{}' requested an unsupported extension: {}", dev_id.value, extension_id)});
	return nullptr;
}

[[nodiscard]] static
auto get_host_data(const clap_host_t* host) -> device_host_data& {
	return *static_cast<device_host_data*>(host->host_data);
}

static
auto cb_params_rescan(ez::main_t, sbox::app* app, id::device dev_id, clap_param_rescan_flags flags) -> void {
	send_msg(ez::main, app, dev_id, device_msg::params_rescan{flags});
}

static
auto cb_request_param_flush(ez::safe_t, sbox::app* app, id::device dev_id) -> void {
	if (const auto dev = app->model.read(ez::safe)->clap_devices.find(dev_id)) {
		dev->service.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_param_flush, std::memory_order_relaxed);
	}
}

static
auto cb_request_process(ez::safe_t, sbox::app* app, id::device dev_id) -> void {
	if (const auto dev = app->model.read(ez::safe)->clap_devices.find(dev_id)) {
		dev->service.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_active | device_atomic_flags::schedule_process);
	}
}

static
auto cb_request_restart(ez::safe_t, sbox::app* app, id::device dev_id) -> void {
	if (const auto dev = app->model.read(ez::safe)->clap_devices.find(dev_id)) {
		dev->service.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_restart);
	}
}

static
auto cb_request_callback(ez::safe_t, sbox::app* app, id::device dev_id) -> void {
	if (const auto dev = app->model.read(ez::safe)->clap_devices.find(dev_id)) {
		dev->service.data->atomic_flags.value.fetch_or(device_atomic_flags::schedule_callback);
	}
}

static
auto cb_gui_closed(ez::safe_t, sbox::app* app, id::device dev_id, bool was_destroyed) -> void {
	send_msg(ez::safe, app, dev_id, device_msg::gui_closed{was_destroyed});
}

static
auto cb_gui_request_hide(ez::safe_t, sbox::app* app, id::device dev_id) -> void {
	send_msg(ez::safe, app, dev_id, device_msg::gui_request_hide{});
}

static
auto cb_gui_request_show(ez::safe_t, sbox::app* app, id::device dev_id) -> void {
	send_msg(ez::safe, app, dev_id, device_msg::gui_request_show{});
}

static
auto cb_gui_request_resize(ez::safe_t, sbox::app* app, id::device dev_id, uint32_t width, uint32_t height) -> void {
	send_msg(ez::safe, app, dev_id, device_msg::gui_request_resize{{width, height}});
}

static
auto cb_gui_resize_hints_changed(ez::safe_t, sbox::app* app, id::device dev_id) -> void {
	send_msg(ez::safe, app, dev_id, device_msg::gui_resize_hints_changed{});
}

static
auto cb_get_track_info(ez::main_t, sbox::app* app, id::device dev_id, clap_track_info* out) -> void {
	*out = {0};
	if (const auto dev = app->model.read(ez::main).devices.find(dev_id)) {
		if (dev->track_color) {
			out->color.red   = dev->track_color->r;
			out->color.green = dev->track_color->g;
			out->color.blue  = dev->track_color->b;
			out->color.alpha = dev->track_color->a;
			out->flags |= CLAP_TRACK_INFO_HAS_TRACK_COLOR;
		}
		if (!dev->track_name->empty()) {
			const auto length = std::min(dev->track_name->size(), size_t(CLAP_NAME_SIZE - 1));
			std::copy_n(dev->track_name->c_str(), length, out->name);
			out->name[length] = '\0';
			out->flags |= CLAP_TRACK_INFO_HAS_TRACK_NAME;
		}
	}
}

static
auto convert_input_events(ez::safe_t, const sbox::device& dev, const clap::device& clap_dev) -> void {
	auto get_cookie = [dev](idx::param param) -> void* {
		return dev.param_info[param.value].clap.cookie;
	};
	auto get_id = [dev](idx::param param) -> clap_id {
		return dev.param_info[param.value].id.value;
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
auto convert_output_events(ez::safe_t, const sbox::device& dev, const clap::device& clap_dev) -> void {
	auto find_param = [dev](clap_id id) -> idx::param {
		auto has_id = [id](const scuff::sbox_param_info& info) -> bool {
			return info.id.value == id;
		};
		const auto pos = std::ranges::find_if(dev.param_info, has_id);
		if (pos == std::end(dev.param_info)) {
			throw std::runtime_error(std::format("Could not find parameter with CLAP id: {}", id));
		}
		return static_cast<idx::param>(std::distance(std::begin(dev.param_info), pos));
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
auto flush_device_events(ez::safe_t, const sbox::device& dev, const clap::device& clap_dev) -> void {
	const auto& input_events  = clap_dev.service.audio->input_events;
	const auto& output_events = clap_dev.service.audio->output_events;
	const auto& iface         = clap_dev.iface->plugin;
	if (!iface.params) {
		// May not actually be intialized
		return;
	}
	convert_input_events(ez::safe, dev, clap_dev);
	iface.params->flush(iface.plugin, &input_events, &output_events);
	convert_output_events(ez::safe, dev, clap_dev);
}

[[nodiscard]] static
auto can_render_audio(ez::audio_t, const clap::audio_buffers& buffers) -> bool {
	if (buffers.inputs.buffers.empty())                { return false; }
	if (buffers.outputs.buffers.empty())               { return false; }
	if (buffers.inputs.buffers[0].channel_count == 0)  { return false; }
	if (buffers.outputs.buffers[0].channel_count == 0) { return false; }
	return true;
}

[[nodiscard]] static
auto try_to_wake_up(ez::audio_t, const clap::device& dev) -> bool {
	unset_flags(&dev.service.data->atomic_flags, device_atomic_flags::schedule_process);
	if (!dev.iface->plugin.plugin->start_processing(dev.iface->plugin.plugin)) {
		return false;
	}
	set_flags(&dev.service.data->atomic_flags, device_atomic_flags::processing);
	return true;
}

[[nodiscard]] static
auto output_is_quiet(ez::audio_t, const shm::device& shm) -> bool {
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
auto go_to_sleep(ez::audio_t, const clap::device& dev) -> void {
	dev.iface->plugin.plugin->stop_processing(dev.iface->plugin.plugin);
	unset_flags(&dev.service.data->atomic_flags, device_atomic_flags::processing);
}

static
auto handle_audio_process_result(ez::audio_t, const shm::device& shm, const clap::device& dev, clap_process_status status) -> void {
	switch (status) {
		case CLAP_PROCESS_CONTINUE: {
			return;
		}
		case CLAP_PROCESS_CONTINUE_IF_NOT_QUIET: {
			if (output_is_quiet(ez::audio, shm)) {
				go_to_sleep(ez::audio, dev);
			}
			return;
		}
		default:
		case CLAP_PROCESS_ERROR:
		case CLAP_PROCESS_TAIL:
		case CLAP_PROCESS_SLEEP: {
			go_to_sleep(ez::audio, dev);
			return;
		}
	}
}

static
auto handle_event_process_result(ez::audio_t, const clap::device& dev, clap_process_status status) -> void {
	switch (status) {
		case CLAP_PROCESS_ERROR:
		case CLAP_PROCESS_CONTINUE: {
			return;
		}
		case CLAP_PROCESS_CONTINUE_IF_NOT_QUIET:
		case CLAP_PROCESS_TAIL:
		case CLAP_PROCESS_SLEEP: {
			go_to_sleep(ez::audio, dev);
			return;
		}
	}
}

static
auto process_audio_device(ez::audio_t, const sbox::device& dev, const clap::device& clap_dev) -> void {
	const auto& iface   = clap_dev.iface->plugin;
	const auto& process = clap_dev.service.audio->process;
	auto& flags         = clap_dev.service.data->atomic_flags;
	auto& audio_buffers = clap_dev.service.audio->buffers;
	convert_input_events(ez::audio, dev, clap_dev);
	const auto status = iface.plugin->process(iface.plugin, &process);
	handle_audio_process_result(ez::audio, dev.service->shm, clap_dev, status);
	convert_output_events(ez::audio, dev, clap_dev);
}

static
auto process_event_device(ez::audio_t, const sbox::device& dev, const clap::device& clap_dev) -> void {
	const auto& iface   = clap_dev.iface->plugin;
	const auto& process = clap_dev.service.audio->process;
	auto& flags         = clap_dev.service.data->atomic_flags;
	convert_input_events(ez::audio, dev, clap_dev);
	const auto status   = iface.plugin->process(iface.plugin, &process);
	handle_event_process_result(ez::audio, clap_dev, status);
	convert_output_events(ez::audio, dev, clap_dev);
}

auto is_scheduled_to_panic(ez::safe_t, const clap::device& device) -> bool {
	return is_flag_set(device.service.data->atomic_flags, device_atomic_flags::schedule_panic);
}

auto panic(ez::audio_t, const clap::device& device) -> void {
	device.iface->plugin.plugin->reset(device.iface->plugin.plugin);
	unset_flags(&device.service.data->atomic_flags, device_atomic_flags::schedule_panic);
}

auto process(ez::audio_t, const sbox::app& app, const sbox::device& dev) -> void {
	const auto& clap_dev = app.audio_model->clap_devices.at(dev.id);
	const auto& iface    = clap_dev.iface->plugin;
	if (!is_active(ez::audio, clap_dev)) {
		return;
	}
	if (is_scheduled_to_panic(ez::audio, clap_dev)) {
		panic(ez::audio, clap_dev);
	}
	if (!is_processing(ez::audio, clap_dev)) {
		flush_device_events(ez::audio, dev, clap_dev);
		if (!is_scheduled_to_process(ez::audio, clap_dev)) {
			return;
		}
		if (!try_to_wake_up(ez::audio, clap_dev)) {
			return;
		}
	}
	if (iface.audio_ports) {
		if (can_render_audio(ez::audio, clap_dev.service.audio->buffers)) {
			process_audio_device(ez::audio, dev, clap_dev);
			return;
		}
		else {
			flush_device_events(ez::audio, dev, clap_dev);
			return;
		}
	}
	process_event_device(ez::audio, dev, clap_dev);
}

static
auto make_audio_buffers(ez::main_t, bc::static_vector<shm::audio_buffer, MAX_AUDIO_PORTS>* shm_buffers, const std::vector<clap_audio_port_info_t>& port_info, audio_buffers_detail* out) -> void {
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
auto make_audio_buffers(ez::main_t, const shm::device& shm, const audio_port_info& port_info, clap::audio_buffers* out) -> void {
	*out = {};
	make_audio_buffers(ez::main, &shm.data->audio_in, port_info.inputs, &out->inputs);
	make_audio_buffers(ez::main, &shm.data->audio_out, port_info.outputs, &out->outputs);
}

[[nodiscard]] static
auto retrieve_audio_port_info(ez::main_t, const iface_plugin& iface) -> audio_port_info {
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
auto make_input_event_list(ez::main_t, const clap::device& dev) -> clap_input_events_t {
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
auto make_output_event_list(ez::main_t, const clap::device& dev) -> clap_output_events_t {
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
auto initialize_process_struct_for_audio_device(ez::main_t, const clap::device& dev, clap::device_service_audio* audio) -> void {
	audio->input_events                = make_input_event_list(ez::main, dev);
	audio->output_events               = make_output_event_list(ez::main, dev);
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
auto initialize_process_struct_for_event_device(ez::main_t, const clap::device& dev, clap::device_service_audio* audio) -> void {
	audio->input_events                = make_input_event_list(ez::main, dev);
	audio->output_events               = make_output_event_list(ez::main, dev);
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
auto init_audio(ez::main_t, const sbox::device& dev, const clap::device& clap_dev) -> std::shared_ptr<const device_service_audio> {
	auto out = std::make_shared<device_service_audio>();
	if (clap_dev.iface->plugin.audio_ports) {
		// AUDIO PLUGIN
		make_audio_buffers(ez::main, dev.service->shm, clap_dev.service.audio_port_info, &out->buffers);
		initialize_process_struct_for_audio_device(ez::main, clap_dev, out.get());
	}
	else {
		// EVENT-ONLY PLUGIN
		initialize_process_struct_for_event_device(ez::main, clap_dev, out.get());
	}
	return out;
}

[[nodiscard]] static
auto init_audio(ez::main_t, clap::device&& clap_dev, const sbox::device& dev) -> device {
	clap_dev.service.audio = init_audio(ez::main, dev, clap_dev);
	return clap_dev;
}

static
auto init_audio(ez::main_t, sbox::app* app, id::device dev_id) -> void {
	app->model.update_publish(ez::main, [=](model&& m) {
		auto dev                         = m.devices.at(dev_id);
		auto clap_dev                    = m.clap_devices.at(dev_id);
		clap_dev.service.audio_port_info = retrieve_audio_port_info(ez::main, clap_dev.iface->plugin);
		clap_dev                         = init_audio(ez::main, std::move(clap_dev), dev);
		m.clap_devices                   = m.clap_devices.insert(clap_dev);
		return m;
	});
}

[[nodiscard]] static
auto make_device_port_info(ez::main_t, const clap::device& clap_dev) -> device_port_info {
	scuff::device_port_info info;
	info.audio_input_port_count  = clap_dev.service.audio_port_info->inputs.size();
	info.audio_output_port_count = clap_dev.service.audio_port_info->outputs.size();
	return info;
}

[[nodiscard]] static
auto make_device_port_info(ez::main_t, const sbox::app& app, id::device dev_id) -> device_port_info {
	const auto m         = app.model.read(ez::main);
	const auto& clap_dev = m.clap_devices.at(dev_id);
	return make_device_port_info(ez::main, clap_dev);
}

static
auto rescan_audio_ports(ez::main_t, sbox::app* app, id::device dev_id, uint32_t flags) -> void {
	const auto requires_not_active =
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_CHANNEL_COUNT) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_FLAGS) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_PORT_TYPE) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_IN_PLACE_PAIR) ||
		is_flag_set(flags, CLAP_AUDIO_PORTS_RESCAN_LIST);
	const auto m   = app->model.read(ez::main);
	const auto dev = m.clap_devices.at(dev_id);
	if (requires_not_active) {
		if (is_active(ez::main, dev)) {
			app->msgs_out.lock()->push_back(scuff::msg::out::report_warning{std::format("Device '{}' tried to rescan audio ports while it is still active!", *dev.name)});
			return;
		}
	}
	init_audio(ez::main, app, dev_id);
	app->msgs_out.lock()->push_back(scuff::msg::out::device_port_info{dev_id.value, make_device_port_info(ez::main, *app, dev_id)});
}

[[nodiscard]] static
auto init_params(ez::main_t, sbox::device&& dev, const clap::device& clap_dev) -> sbox::device {
	const auto& iface = clap_dev.iface->plugin;
	if (iface.params) {
		dev.flags.value |= scuff::device_flags::has_params;
	}
	return dev;
}

[[nodiscard]] static
auto init_params(ez::main_t, clap::device&& dev) -> clap::device {
	const auto& iface = dev.iface->plugin;
	if (iface.params) {
		const auto count = iface.params->count(iface.plugin);
		dev.params = {};
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

[[nodiscard]] static
auto make_local_param_flags(uint32_t clap_flags) -> uint32_t {
	uint32_t out = 0;
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_AUTOMATABLE))             { out |= scuff::param_is_automatable; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL)) { out |= scuff::param_is_automatable_per_channel; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_AUTOMATABLE_PER_KEY))     { out |= scuff::param_is_automatable_per_key; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID)) { out |= scuff::param_is_automatable_per_note_id; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_AUTOMATABLE_PER_PORT))    { out |= scuff::param_is_automatable_per_port; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_BYPASS))                  { out |= scuff::param_is_bypass; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_ENUM))                    { out |= scuff::param_is_enum; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_HIDDEN))                  { out |= scuff::param_is_hidden; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_MODULATABLE))             { out |= scuff::param_is_modulatable; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL)) { out |= scuff::param_is_modulatable_per_channel; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_MODULATABLE_PER_KEY))     { out |= scuff::param_is_modulatable_per_key; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)) { out |= scuff::param_is_modulatable_per_note_id; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_MODULATABLE_PER_PORT))    { out |= scuff::param_is_modulatable_per_port; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_PERIODIC))                { out |= scuff::param_is_periodic; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_READONLY))                { out |= scuff::param_is_readonly; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_PERIODIC))                { out |= scuff::param_is_periodic; }
	if (is_flag_set(clap_flags, CLAP_PARAM_IS_STEPPED))                 { out |= scuff::param_is_stepped; }
	if (is_flag_set(clap_flags, CLAP_PARAM_REQUIRES_PROCESS))           { out |= scuff::param_requires_process; }
	return out;
}

[[nodiscard]] static
auto init_local_params(ez::main_t, sbox::device&& dev, const clap::device& clap_dev) -> sbox::device {
	dev.param_info = {};
	for (size_t i = 0; i < clap_dev.params.size(); i++) {
		const auto& param = clap_dev.params[i];
		scuff::sbox_param_info info;
		info.id            = {param.info.id};
		info.default_value = param.info.default_value;
		info.max_value     = param.info.max_value;
		info.min_value     = param.info.min_value;
		info.clap.cookie   = param.info.cookie;
		info.name          = param.info.name;
		info.flags         = make_local_param_flags(param.info.flags);
		dev.param_info = dev.param_info.push_back(info);
	}
	return dev;
}

static
auto process_msg_(ez::main_t, sbox::app* app, const device& clap_dev, const clap::device_msg::gui_closed& msg) -> void {
	fu::debug_log("clap::device_msg::gui_closed");
	if (msg.destroyed) {
		gui::hide(ez::main, app, app->model.read(ez::main).devices.at(clap_dev.id));
	}
}

static
auto process_msg_(ez::main_t, sbox::app* app, const device& clap_dev, const clap::device_msg::gui_request_hide& msg) -> void {
	const auto& dev = app->model.read(ez::main).devices.at(clap_dev.id);
	gui::hide(ez::main, app, dev);
}

static
auto process_msg_(ez::main_t, sbox::app* app, const device& clap_dev, const clap::device_msg::gui_request_resize& msg) -> void {
	const auto& dev = app->model.read(ez::main).devices.at(clap_dev.id);
	dev.service->scheduled_window_resize = msg.size;
}

static
auto process_msg_(ez::main_t, sbox::app* app, const device& dev, const clap::device_msg::gui_request_show& msg) -> void {
	gui::show(ez::main, app, dev.id, {[]{}});
}

static
auto process_msg_(ez::main_t, sbox::app* app, const device& dev, const clap::device_msg::gui_resize_hints_changed& msg) -> void {
	// FIXME: Stop ignoring this
	fu::debug_log("WARNING: clap_host_gui.resize_hints_changed is currently ignored");
}

static
auto process_msg_(ez::main_t, sbox::app* app, const device& dev, const clap::device_msg::log_begin& msg) -> void {
	dev.service.data->log_collector.severity = msg.severity;
	dev.service.data->log_collector.chunks.clear();
}

static
auto process_msg_(ez::main_t, sbox::app* app, const device& dev, const clap::device_msg::log_end& msg) -> void {
	std::string text;
	for (const auto& chunk : dev.service.data->log_collector.chunks) {
		text.append(chunk.c_str());
	}
	switch (*dev.service.data->log_collector.severity) {
		default:
		case CLAP_LOG_DEBUG:
		case CLAP_LOG_INFO: {
			app->msgs_out.lock()->push_back(scuff::msg::out::report_info{text});
			break;
		}
		case CLAP_LOG_HOST_MISBEHAVING:
		case CLAP_LOG_PLUGIN_MISBEHAVING:
		case CLAP_LOG_ERROR:
		case CLAP_LOG_FATAL: {
			app->msgs_out.lock()->push_back(scuff::msg::out::report_error{text});
			break;
		}
		case CLAP_LOG_WARNING: {
			app->msgs_out.lock()->push_back(scuff::msg::out::report_warning{text});
			break;
		}
	}
	dev.service.data->log_collector.severity = std::nullopt;
	dev.service.data->log_collector.chunks.clear();
}

static
auto process_msg_(ez::main_t, sbox::app* app, const device& dev, const clap::device_msg::log_text& msg) -> void {
	dev.service.data->log_collector.chunks.push_back(msg.text);
}

static
auto process_msg_(ez::main_t, sbox::app* app, clap::device clap_dev, const clap::device_msg::params_rescan& msg) -> void {
	const auto m = app->model.read(ez::main);
	auto dev = m.devices.at(clap_dev.id);
	clap_dev = init_params(ez::main, std::move(clap_dev));
	dev      = init_local_params(ez::main, std::move(dev), clap_dev);
	app->model.update_publish(ez::main, [dev, clap_dev](model&& m){
		m.clap_devices = m.clap_devices.insert(clap_dev);
		m.devices      = m.devices.insert(dev);
		return m;
	});
	app->msgs_out.lock()->push_back(scuff::msg::out::device_param_info{dev.id.value, op::make_client_param_info(dev)});
}

static
auto process_msg(ez::main_t, sbox::app* app, const device& dev, const clap::device_msg::msg& msg) -> void {
	fast_visit([app, dev](const auto& msg) { process_msg_(ez::main, app, dev, msg); }, msg);
}

static
auto update(ez::main_t, sbox::app* app) -> void {
	const auto m = app->model.read(ez::main);
	for (const auto& dev : m.clap_devices) {
		clap::device_msg::msg msg;
		while (dev.service.data->msg_q.try_dequeue(msg)) {
			process_msg(ez::main, app, dev, msg);
		}
	}
}

static
auto make_host_for_instance(ez::main_t, device_host_data* host_data) -> void {
	host_data->iface.host.clap_version = CLAP_VERSION;
	host_data->iface.host.name         = "scuff-sbox";
	host_data->iface.host.url          = "https://github.com/colugomusic/scuff";
	host_data->iface.host.vendor       = "Moron Enterprises";
	host_data->iface.host.host_data    = host_data;
	host_data->iface.host.get_extension = [](const clap_host_t* host, const char* extension_id) -> const void* {
		const auto& hd = get_host_data(host);
		return get_extension(ez::safe, hd.app, hd.iface, hd.dev_id, extension_id);
	};
	host_data->iface.host.request_callback = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_callback(ez::safe, hd.app, hd.dev_id);
	};
	host_data->iface.host.request_process = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_process(ez::safe, hd.app, hd.dev_id);
	};
	host_data->iface.host.request_restart = [](const clap_host* host) {
		const auto& hd = get_host_data(host);
		cb_request_restart(ez::safe, hd.app, hd.dev_id);
	};
	// AUDIO PORTS ______________________________________________________________
	host_data->iface.audio_ports.is_rescan_flag_supported = [](const clap_host* host, uint32_t flag) -> bool {
		return true;
	};
	host_data->iface.audio_ports.rescan = [](const clap_host* host, uint32_t flags) -> void {
		const auto& hd = get_host_data(host);
		rescan_audio_ports(ez::main, hd.app, hd.dev_id, flags);
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
		cb_gui_closed(ez::safe, hd.app, hd.dev_id, was_destroyed);
	};
	host_data->iface.gui.request_hide = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_hide(ez::safe, hd.app, hd.dev_id);
		return true;
	};
	host_data->iface.gui.request_show = [](const clap_host* host) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_show(ez::safe, hd.app, hd.dev_id);
		return true;
	};
	host_data->iface.gui.request_resize = [](const clap_host* host, uint32_t width, uint32_t height) -> bool {
		const auto& hd = get_host_data(host);
		cb_gui_request_resize(ez::safe, hd.app, hd.dev_id, width, height);
		return true;
	};
	host_data->iface.gui.resize_hints_changed = [](const clap_host* host) -> void {
		const auto& hd = get_host_data(host);
		cb_gui_resize_hints_changed(ez::safe, hd.app, hd.dev_id);
	};
	// LATENCY __________________________________________________________________
	host_data->iface.latency.changed = [](const clap_host* host) -> void {
		// I don't do anything with this yet
	};
	// LOG ----------------------------------------------------------------------
	host_data->iface.log.log = [](const clap_host* host, clap_log_severity severity, const char* msg) -> void {
		const auto& hd = get_host_data(host);
		clap::log(ez::safe, hd.app, hd.dev_id, severity, msg);
	};
	// PARAMS ___________________________________________________________________
	host_data->iface.params.clear = [](const clap_host* host, clap_id param_id, clap_param_clear_flags flags) -> void {
		// I don't do anything with this yet
	};
	host_data->iface.params.request_flush = [](const clap_host* host) -> void {
		const auto& hd = get_host_data(host);
		cb_request_param_flush(ez::safe, hd.app, hd.dev_id);
	};
	host_data->iface.params.rescan = [](const clap_host* host, clap_param_rescan_flags flags) -> void {
		const auto& hd = get_host_data(host);
		cb_params_rescan(ez::main, hd.app, hd.dev_id, flags);
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
		cb_get_track_info(ez::main, hd.app, hd.dev_id, info);
		return true;
	};
}

static
auto get_extensions(ez::main_t, clap::iface_plugin* iface) -> void {
	iface->audio_ports  = scuff::get_plugin_ext<clap_plugin_audio_ports_t>(*iface->plugin, CLAP_EXT_AUDIO_PORTS);
	iface->context_menu = scuff::get_plugin_ext<clap_plugin_context_menu_t>(*iface->plugin, CLAP_EXT_CONTEXT_MENU, CLAP_EXT_CONTEXT_MENU_COMPAT);
	iface->gui          = scuff::get_plugin_ext<clap_plugin_gui_t>(*iface->plugin, CLAP_EXT_GUI);
	iface->params       = scuff::get_plugin_ext<clap_plugin_params_t>(*iface->plugin, CLAP_EXT_PARAMS);
	iface->render       = scuff::get_plugin_ext<clap_plugin_render_t>(*iface->plugin, CLAP_EXT_RENDER);
	iface->state        = scuff::get_plugin_ext<clap_plugin_state_t>(*iface->plugin, CLAP_EXT_STATE);
	iface->tail         = scuff::get_plugin_ext<clap_plugin_tail_t>(*iface->plugin, CLAP_EXT_TAIL);
}

[[nodiscard]] static
auto init_gui(ez::main_t, sbox::device&& dev, const clap::device& clap_dev) -> sbox::device {
	const auto& iface = clap_dev.iface->plugin;
	if (iface.gui) {
		if (iface.gui->is_api_supported(iface.plugin, scuff::os::get_clap_window_api(), false)) {
			dev.flags.value |= scuff::device_flags::has_gui;
			return dev;
		}
	}
	return dev;
}

[[nodiscard]] static
auto make_ext_data(ez::main_t, sbox::app* app, id::device id) -> std::shared_ptr<clap::device_service_data> {
	auto data = std::make_shared<clap::device_service_data>();
	data->host_data.app    = app;
	data->host_data.dev_id = id;
	data->input_events_context.service_data  = data.get();
	data->output_events_context.service_data = data.get();
	return data;
}

[[nodiscard]] static
auto make_shm_device(ez::main_t, std::string_view sbox_shmid, id::device dev_id, sbox::mode mode) -> shm::device {
	const auto remove_when_done = mode != sbox::mode::sandbox;
	return shm::open_or_create_device(shm::make_device_id(sbox_shmid, dev_id), remove_when_done);
}

static
auto create_device(ez::main_t, sbox::app* app, id::device dev_id, std::string_view plugfile_path, std::string_view plugin_id) -> void {
	const auto entry = scuff::os::dso::find_fn<clap_plugin_entry_t>({plugfile_path}, {CLAP_SYMBOL_ENTRY});
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
	if (plugin_id == "ANY") {
		if (factory->get_plugin_count(factory) < 1) {
			entry->deinit();
			throw std::runtime_error("plugfile has no plugins");
		}
		plugin_id = factory->get_plugin_descriptor(factory, 0)->id;
	}
	auto ext_data = make_ext_data(ez::main, app, dev_id);
	make_host_for_instance(ez::main, &ext_data->host_data);
	clap::iface iface;
	iface.plugin.plugin = factory->create_plugin(factory, &ext_data->host_data.iface.host, plugin_id.data());
	if (!iface.plugin.plugin) {
		entry->deinit();
		throw std::runtime_error("clap_plugin_factory.create_plugin failed");
	}
	if (!iface.plugin.plugin->init(iface.plugin.plugin)) {
		throw std::runtime_error("clap_plugin.init failed");
	}
	get_extensions(ez::main, &iface.plugin);
	auto dev                         = sbox::device{};
	auto clap_dev                    = clap::device{};
	dev.id                           = dev_id;
	dev.type                         = plugin_type::clap;
	dev.service->shm = make_shm_device(ez::main, app->shm_sbox.seg.id, dev_id, app->mode);
	clap_dev.service.audio_port_info = retrieve_audio_port_info(ez::main, iface.plugin);
	const auto audio_in_count    = clap_dev.service.audio_port_info->inputs.size();
	const auto audio_out_count   = clap_dev.service.audio_port_info->outputs.size();
	dev.service->shm.data->audio_in.resize(audio_in_count);
	dev.service->shm.data->audio_out.resize(audio_out_count);
	clap_dev.id           = dev_id;
	clap_dev.iface        = std::move(iface);
	clap_dev.name         = clap_dev.iface->plugin.plugin->desc->name;
	dev.name              = clap_dev.name;
	clap_dev.service.data = std::move(ext_data);
	dev                   = init_gui(ez::main, std::move(dev), clap_dev);
	dev                   = init_params(ez::main, std::move(dev), clap_dev);
	clap_dev              = init_audio(ez::main, std::move(clap_dev), dev);
	clap_dev              = init_params(ez::main, std::move(clap_dev));
	dev                   = init_local_params(ez::main, std::move(dev), clap_dev);
	app->model.update_publish(ez::main, [=](model&& m) {
		m.devices      = m.devices.insert(dev);
		m.clap_devices = m.clap_devices.insert(clap_dev);
		return m;
	});
}

[[nodiscard]] static
auto get_param_value(ez::main_t, const sbox::app& app, id::device dev_id, idx::param param_idx) -> std::optional<double> {
	const auto dev = app.model.read(ez::main).clap_devices.at(dev_id);
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
auto get_param_value_text(ez::main_t, const sbox::app& app, id::device dev_id, idx::param param_idx, double value) -> std::string {
	static constexpr auto BUFFER_SIZE = 50;
	const auto dev = app.model.read(ez::main).clap_devices.at(dev_id);
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
auto load(ez::main_t, sbox::app* app, id::device dev_id, const std::vector<std::byte>& state) -> bool {
	const auto dev = app->model.read(ez::main).clap_devices.at(dev_id);
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
auto save(ez::main_t, sbox::app* app, id::device dev_id) -> std::vector<std::byte> {
	const auto dev = app->model.read(ez::main).clap_devices.at(dev_id);
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
auto activate(ez::main_t, sbox::app* app, id::device dev_id, double sr) -> bool {
	const auto m              = app->model.read(ez::main);
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
	app->model.update_publish(ez::main, [dev_id, sr](model&& m) {
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
	clap_dev.service.data->atomic_flags.value |= device_atomic_flags::schedule_process;
	return true;
}

static
auto deactivate(ez::main_t, sbox::app* app, id::device dev_id) -> void {
	const auto m         = app->model.read(ez::main);
	const auto clap_dev  = m.clap_devices.at(dev_id);
	const auto is_active = clap_dev.flags.value & device_flags::active;
	if (!is_active) {
		return;
	}
	app->model.update_publish(ez::main, [dev_id](model&& m) {
		m.clap_devices = m.clap_devices.update(dev_id, [](clap::device clap_dev) {
			clap_dev.flags.value &= ~device_flags::active;
			return clap_dev;
		});
		return m;
	});
	clap_dev.iface->plugin.plugin->deactivate(clap_dev.iface->plugin.plugin);
	clap_dev.service.data->atomic_flags.value &= ~device_atomic_flags::processing;
}

[[nodiscard]] static
auto create_gui(ez::main_t, sbox::app* app, const sbox::device& dev) -> sbox::create_gui_result {
	fu::debug_log("INFO: clap::main::create_gui");
	const auto m         = app->model.read(ez::main);
	const auto clap_dev  = m.clap_devices.at(dev.id);
	const auto iface     = clap_dev.iface->plugin;
	if (!iface.gui) {
		fu::log("ERROR: iface.gui is null?");
		return {};
	}
	if (!iface.gui->create(iface.plugin, scuff::os::get_clap_window_api(), false)) {
		fu::log("ERROR: iface.gui->create failed");
		return {};
	}
	fu::debug_log("INFO: iface.gui->create succeeded");
	uint32_t width = 5000, height = 5000;
	if (!iface.gui->get_size(iface.plugin, &width, &height)) {
		fu::log("ERROR: iface.gui->get_size failed");
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
auto setup_editor_window(ez::main_t, sbox::app* app, const sbox::device& dev) -> bool {
	fu::debug_log("INFO: clap::main::setup_editor_window");
	const auto m          = app->model.read(ez::main);
	const auto clap_dev   = m.clap_devices.at(dev.id);
	const auto iface      = clap_dev.iface->plugin;
	const auto window_ref = os::make_clap_window_ref(dev.ui.window);
	if (!iface.gui->set_parent(iface.plugin, &window_ref)) {
		fu::log("ERROR: iface.gui->set_parent failed");
		return false;
	}
	fu::debug_log("INFO: iface.gui->set_parent succeeded");
	// This return value is ignored because some Plugins return false
	// even if the window is shown.
	iface.gui->show(iface.plugin);
	return true;
}

static
auto destroy(ez::main_t, const sbox::model& m, const sbox::device& dev) -> void {
	const auto clap_dev = m.clap_devices.at(dev.id);
	const auto iface     = clap_dev.iface->plugin;
	iface.plugin->deactivate(iface.plugin);
	iface.plugin->destroy(iface.plugin);
}

static
auto shutdown_editor_window(ez::main_t, sbox::app* app, const sbox::device& dev) -> void {
	const auto m        = app->model.read(ez::main);
	const auto clap_dev = m.clap_devices.at(dev.id);
	const auto iface     = clap_dev.iface->plugin;
	if (!iface.gui) {
		return;
	}
	iface.gui->hide(iface.plugin);
	iface.gui->destroy(iface.plugin);
}

[[nodiscard]] static
auto get_gui_size(ez::main_t, const clap::iface_plugin& iface) -> std::optional<window_size_u32> {
	uint32_t old_width, old_height;
	if (iface.gui->get_size(iface.plugin, &old_width, &old_height)) {
		return window_size_u32{old_width, old_height};
	}
	return std::nullopt;
}

static
auto on_native_window_resize(ez::main_t, const sbox::app* app, const sbox::device& dev, edwin::size native_window_size) -> void {
	const auto m                = app->model.read(ez::main);
	const auto& clap_dev        = m.clap_devices.at(dev.id);
	const auto iface            = clap_dev.iface->plugin;
	const auto old_size         = get_gui_size(ez::main, iface);
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

auto panic(ez::main_t, sbox::app* app, id::device dev_id, double sr) -> void {
	const auto& device = app->model.read(ez::main).clap_devices.at(dev_id);
	set_flags(&device.service.data->atomic_flags, device_atomic_flags::schedule_panic);
}

auto set_render_mode(ez::main_t, sbox::app* app, id::device dev_id, scuff::render_mode mode) -> void {
	const auto m = app->model.read(ez::main);
	const auto clap_dev = m.clap_devices.at(dev_id);
	const auto iface = clap_dev.iface->plugin;
	if (!iface.render) {
		return;
	}
	if (mode == render_mode::offline && iface.render->has_hard_realtime_requirement(iface.plugin)) {
		return;
	}
	const auto clap_mode = mode == render_mode::offline ? CLAP_RENDER_OFFLINE : CLAP_RENDER_REALTIME;
	iface.render->set(iface.plugin, clap_mode);
}

} // scuff::sbox::clap