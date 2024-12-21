#include "client.hpp"
#include "common-os.hpp"
#include "common-signaling.hpp"
#include "common-types.hpp"
#include "common-visit.hpp"
#include "managed.hpp"
#include "scan.hpp"
#include <clap/plugin-features.h>
#include <mutex>
#include <platform_folders.h>
#include <readerwriterqueue.h>
#include <source_location>
#include <string>
#include <variant>

#define SCUFF_EXCEPTION_WRAPPER \
	catch (const std::exception& err) { throw scuff::runtime_error(std::source_location::current().function_name(), err.what()); } \
	catch (...)                       { throw scuff::runtime_error(std::source_location::current().function_name(), "Unknown error"); }

namespace bip = boost::interprocess;
namespace fs  = std::filesystem;

namespace scuff::impl {

using poll_t = ez::nort_t;
static constexpr auto poll = poll_t{};

[[nodiscard]] static
auto make_sbox_exe_args(std::string_view pid, std::string_view group_id, std::string_view sandbox_id, uint64_t parent_window) -> std::vector<std::string> {
	std::vector<std::string> args;
	args.push_back("--pid");
	args.push_back(std::string{pid});
	args.push_back("--group");
	args.push_back(std::string{group_id});
	args.push_back("--sandbox");
	args.push_back(std::string{sandbox_id});
	args.push_back("--parent-window");
	args.push_back(std::to_string(parent_window));
	return args;
}

static
auto intercept_input_event(ez::safe_t, const scuff::device& dev, const scuff::event& event) -> void {
	if (std::holds_alternative<scuff::events::param_value>(event)) {
		dev.service->dirty_marker++;
	}
}

static
auto intercept_output_event(ez::safe_t, const scuff::device& dev, const scuff::event& event) -> void {
	if (std::holds_alternative<scuff::events::param_value>(event)) {
		dev.service->dirty_marker++;
	}
}

static
auto write_audio_input(ez::audio_t, const scuff::model& m, const scuff::audio_input& input) -> void {
	if (const auto dev = m.devices.find(input.dev_id)) {
		if (dev->flags.value & client_device_flags::has_remote) {
			auto& buffer   = dev->service->shm.data->audio_in[input.port_index];
			input.write_to(buffer.data());
		}
	}
}

static
auto write_audio_inputs(ez::audio_t, const scuff::model& m, const scuff::audio_inputs& inputs) -> void {
	for (const auto& input : inputs) {
		write_audio_input(ez::audio, m, input);
	}
}

static
auto write_input_events(ez::audio_t, const scuff::model& m, const scuff::input_events& input_events) -> void {
	bc::static_vector<scuff::input_event, EVENT_PORT_SIZE> event_buffer;
	event_buffer.resize(input_events.count());
	const auto events_to_pop = std::min(size_t(EVENT_PORT_SIZE), event_buffer.size());
	const auto events_popped = input_events.pop(events_to_pop, event_buffer.data());
	event_buffer.resize(events_popped);
	for (const auto& event : event_buffer) {
		if (const auto dev = m.devices.find(event.device_id)) {
			intercept_input_event(ez::audio, *dev, event.event);
			dev->service->shm.data->events_in.push_back(event.event);
		}
	}
}

static
auto process_inputs(ez::audio_t, const scuff::model& m, const scuff::audio_inputs& audio_inputs, const scuff::input_events& input_events) -> void {
	write_audio_inputs(ez::audio, m, audio_inputs);
	write_input_events(ez::audio, m, input_events);
}

static
auto read_audio_output(ez::audio_t, const scuff::model& m, const audio_output& output) -> void {
	if (const auto dev = m.devices.find(output.dev_id)) {
		if (dev->flags.value & client_device_flags::has_remote) {
			const auto& buffer = dev->service->shm.data->audio_out[output.port_index];
			output.read_from(buffer.data());
		}
	}
}

static
auto read_audio_outputs(ez::audio_t, const scuff::model& m, const audio_outputs& output) -> void {
	for (const auto& output : output) {
		read_audio_output(ez::audio, m, output);
	}
}

static
auto read_zeros(ez::audio_t, const scuff::model& m, const audio_outputs& outputs) -> void {
	std::array<float, CHANNEL_COUNT * VECTOR_SIZE> zeros = {0.0f};
	for (const auto& output : outputs) {
		output.read_from(zeros.data());
	}
}

static
auto read_output_events(ez::audio_t, const scuff::model& m, const scuff::group& group, const output_events& output_events) -> void {
	for (const auto sbox_id : group.sandboxes) {
		const auto& sbox = m.sandboxes.at(sbox_id);
		for (const auto dev_id : sbox.devices) {
			const auto& dev = m.devices.at(dev_id);
			if (dev.flags.value & client_device_flags::has_remote) {
				auto& buffer = dev.service->shm.data->events_out;
				for (const auto& event : buffer) {
					intercept_output_event(ez::audio, dev, event);
					output_events.push({dev_id, event});
				}
				buffer.clear();
			}
		}
	}
}

static
auto process_cross_sbox_connections(ez::audio_t, const scuff::model& m, const scuff::group& group) -> void {
	for (const auto& conn : group.cross_sbox_conns) {
		const auto& dev_out = m.devices.at(conn.out_dev_id);
		const auto& dev_in  = m.devices.at(conn.in_dev_id);
		if ((dev_out.flags.value & dev_in.flags.value) & client_device_flags::has_remote) {
			const auto& out_buf = dev_out.service->shm.data->audio_out[conn.out_port];
			auto& in_buf        = dev_in.service->shm.data->audio_in[conn.in_port];
			std::copy(out_buf.begin(), out_buf.end(), in_buf.begin());
		}
	}
}

static
auto process_outputs(ez::audio_t, const scuff::model& m, const scuff::group& group, const scuff::audio_outputs& audio_outputs, const scuff::output_events& output_events) -> void {
	read_audio_outputs(ez::audio, m, audio_outputs);
	read_output_events(ez::audio, m, group, output_events);
	process_cross_sbox_connections(ez::audio, m, group);
}

[[nodiscard]] static
auto confirmed_active(const sandbox& sbox) -> bool {
	return sbox.flags.value & sandbox_flags::confirmed_active;
}

[[nodiscard]] static
auto launched(const sandbox& sbox) -> bool {
	return sbox.flags.value & sandbox_flags::launched;
}

[[nodiscard]] static
auto get_active_sandbox_count(const model& m, const scuff::group& group) -> int {
	int total = 0;
	for (const auto sbox_id : group.sandboxes) {
		const auto& sbox = m.sandboxes.at(sbox_id);
		if (launched(sbox) && confirmed_active(sbox) && sbox.service->proc.running()) {
			total++;
		}
	}
	return total;
}

static
auto zero_inactive_device_outputs(ez::audio_t, const ez::immutable<model>& audio, const scuff::group& group) -> void {
	const auto group_is_active = group.flags.value & group_flags::is_active;
	for (const auto sbox_id : group.sandboxes) {
		const auto& sbox = audio->sandboxes.at(sbox_id);
		for (const auto dev_id : sbox.devices) {
			const auto& dev = audio->devices.at(dev_id);
			const auto& shm = dev.service->shm;
			if (!shm::is_valid(shm.seg)) {
				// Device may not have finished being created yet.
				continue;
			}
			if (group_is_active) {
				// Device is active so it should be outputting audio.
				continue;
			}
			// Device is not active so zero its output buffers.
			for (auto& buffer : shm.data->audio_out) {
				buffer.fill(0.0f);
			}
		}
	}
}

[[nodiscard]] static
auto do_sandbox_processing(ez::audio_t, const ez::immutable<model>& audio, const scuff::group& group) -> bool {
	auto sandbox_iterator = group.sandboxes.begin();
	auto next_sandbox_signal = [&sandbox_iterator, &audio]() -> const ipc::local_event& {
		const auto sandbox_id = *sandbox_iterator;
		const auto& sandbox = audio->sandboxes.at(sandbox_id);
		return sandbox.service->shm.signaling.work_begin;
	};
	if (!signaling::sandboxes_work_begin(group.service->signaler, group.total_active_sandboxes, next_sandbox_signal)) {
		return false;
	}
	zero_inactive_device_outputs(ez::audio, audio, group);
	if (group.total_active_sandboxes <= 0) {
		return true;
	}
	const auto result = signaling::wait_for_all_sandboxes_done(group.service->signaler);
	switch (result) {
		case signaling::client_wait_result::done: {
			return true;
		}
		case signaling::client_wait_result::not_responding: {
			return false;
		}
		default: {
			throw std::runtime_error("Unexpected wait_for_sandboxes_done_result value.");
		}
	}
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::confirm_activated& msg) -> void {
	DATA_->model.update_publish(ez::nort, [sbox = sbox](model&& m) mutable {
		sbox.flags.value |= sandbox_flags::confirmed_active;
		m.sandboxes       = m.sandboxes.insert(sbox);
		auto group                   = m.groups.at(sbox.group);
		group.total_active_sandboxes = get_active_sandbox_count(m, group);
		m.groups                     = m.groups.insert(group);
		return m;
	});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::device_create_fail& msg) -> void {
	// The sandbox failed to create the remote device.
	DATA_->model.update_publish(ez::nort, [sbox, msg](model&& m){
		m = set_error(std::move(m), {msg.dev_id}, "Failed to create remote device.");
		return m;
	});
	sbox.service->return_buffers.device_create_results.take(msg.callback)({msg.dev_id, false});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::device_create_success& msg) -> void {
	// The sandbox succeeded in creating the remote device.
	DATA_->model.update_publish(ez::nort, [sbox, msg](model&& m){
		auto device             = m.devices.at({msg.dev_id});
		const auto& sbox        = m.sandboxes.at(device.sbox);
		const auto device_shmid = shm::make_device_id(sbox.service->get_shmid(), {msg.dev_id});
		if (!shm::is_valid(device.service->shm.seg)) {
			// Only open the shared memory segment if it's not already open. If we got here as
			// the result of a sandbox being restarted then we will already have the shared memory
			// open.
			device.service->shm = shm::open_device(device_shmid, true);
		}
		device.flags.value |= client_device_flags::has_remote;
		m.devices = m.devices.insert(device);
		return m;
	});
	sbox.service->return_buffers.device_create_results.take(msg.callback)({msg.dev_id, true});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::device_load_fail& msg) -> void {
	ui::send(sbox, ui::msg::device_state_load{{msg.dev_id, false}});
	ui::send(sbox, ui::msg::sbox_error{sbox.id, "Failed to load device."});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::device_load_success& msg) -> void {
	ui::send(sbox, ui::msg::device_state_load{{msg.dev_id, true}});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::device_editor_visible_changed& msg) -> void {
	ui::send(sbox, ui::msg::device_editor_visible_changed{{msg.dev_id}, msg.visible, msg.native_handle});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::device_flags& msg) -> void {
	DATA_->model.update(ez::nort, [msg](model&& m) {
		m.devices = m.devices.update({msg.dev_id}, [msg](device dev) {
			if (msg.flags & device_flags::has_gui)    { dev.flags.value |= client_device_flags::has_gui; }
			if (msg.flags & device_flags::has_params) { dev.flags.value |= client_device_flags::has_params; }
			return dev;
		});
		return m;
	});
	ui::send(sbox, ui::msg::device_flags_changed{{msg.dev_id}});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::device_port_info& msg) -> void {
	DATA_->model.update(ez::nort, [msg](model&& m) {
		m.devices = m.devices.update_if_exists({msg.dev_id}, [msg](device dev) {
			dev.port_info = msg.info;
			return dev;
		});
		return m;
	});
	ui::send(sbox, ui::msg::device_ports_changed{{msg.dev_id}});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::device_param_info& msg) -> void {
	DATA_->model.update_publish(ez::nort, [msg](model&& m) {
		m.devices = m.devices.update_if_exists({msg.dev_id}, [msg](device dev) {
			dev.param_info = {};
			for (const auto& info : msg.info) {
				dev.param_info = dev.param_info.push_back(info);
			}
			return dev;
		});
		return m;
	});
	ui::send(sbox, ui::msg::device_params_changed{{msg.dev_id}});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::report_error& msg) -> void {
	ui::send(sbox, ui::msg::sbox_error{sbox.id, msg.text});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::report_info& msg) -> void {
	ui::send(sbox, ui::msg::sbox_info{sbox.id, msg.text});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::report_warning& msg) -> void {
	ui::send(sbox, ui::msg::sbox_warning{sbox.id, msg.text});
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::return_param_value& msg) -> void {
	sbox.service->return_buffers.doubles.take(msg.callback)(msg.value);
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::return_param_value_text& msg) -> void {
	sbox.service->return_buffers.strings.take(msg.callback)(msg.text);
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::return_state& msg) -> void {
	sbox.service->return_buffers.states.take(msg.callback)(msg.bytes);
}

static
auto msg_from_sandbox(poll_t, const sandbox& sbox, const msg::out::msg& msg) -> void {
	 const auto proc = [sbox](const auto& msg) -> void { msg_from_sandbox(poll, sbox, msg); };
	 try                               { fast_visit(proc, msg); }
	 catch (const std::exception& err) { ui::send(sbox, ui::msg::error{err.what()}); }
}

static
auto process_sandbox_messages(poll_t, const sandbox& sbox) -> void {
	if (launched(sbox) && !sbox.service->proc.running()) {
		DATA_->model.update_publish(poll, [sbox = sbox](model&& m) mutable {
			sbox.flags.value &= ~sandbox_flags::launched;
			m.sandboxes       = m.sandboxes.insert(sbox);
			for (const auto dev_id : sbox.devices) {
				m.devices = m.devices.update(dev_id, [](device dev) {
					dev.editor_window_native_handle = nullptr;
					dev.flags.value &= ~client_device_flags::has_remote;
					return dev;
				});
			}
			m.groups = m.groups.update_if_exists(sbox.group, [m](scuff::group g) {
				g.total_active_sandboxes = get_active_sandbox_count(m, g);
				return g;
			});
			return m;
		});
		const auto m = DATA_->model.read(poll);
		if (const auto group = m.groups.find(sbox.group)) {
			signaling::unblock_self(group->service->signaler);
		}
		ui::send(sbox, ui::msg::sbox_crashed{sbox.id, "Sandbox process stopped unexpectedly."});
		return;
	}
	if (sbox.service) {
		sbox.service->send_msgs_to_sandbox();
		const auto& msgs = sbox.service->receive_msgs_from_sandbox();
		for (const auto& msg : msgs) {
			msg_from_sandbox(poll, sbox, msg);
		}
	}
}

static
auto process_sandbox_messages(poll_t) -> void {
	const auto sandboxes = DATA_->model.read(poll).sandboxes;
	for (const auto& sbox : sandboxes) {
		process_sandbox_messages(poll, sbox);
	}
}

static
auto update_saved_state_with_returned_bytes(ez::nort_t, id::device dev_id, const scuff::bytes& bytes) -> void {
	DATA_->model.update(ez::nort, [dev_id, &bytes](model&& m){
		if (auto ptr = m.devices.find(dev_id)) {
			auto dev = *ptr;
			dev.last_saved_state = bytes;
			m.devices = m.devices.insert(dev);
		}
		return m;
	});
}

static
auto save_async(ez::nort_t, const scuff::device& dev, return_bytes fn) -> void {
	const auto m = DATA_->model.read(ez::nort);
	const auto sbox = m.sandboxes.at(dev.sbox);
	auto wrapper_fn = [dev_id = dev.id, sbox, fn](const scuff::bytes& bytes){
		update_saved_state_with_returned_bytes(ez::nort, dev_id, bytes);
		ui::send(sbox, ui::msg::return_device_state{bytes, fn});
	};
	sbox.service->enqueue(msg::in::device_save{dev.id.value, sbox.service->return_buffers.states.put(wrapper_fn)});
}

static
auto save_async(ez::nort_t, id::device dev_id, return_bytes fn) -> void {
	const auto m = DATA_->model.read(ez::nort);
	save_async(ez::nort, m.devices.at(dev_id), fn);
}

static
auto save_dirty_device_state(poll_t, const scuff::device& dev) -> void {
	const auto dirty_marker = dev.service->dirty_marker.load();
	const auto saved_marker = dev.service->saved_marker.load();
	if (dirty_marker > saved_marker) {
		auto with_bytes = [dev_id = dev.id, dirty_marker](const scuff::bytes& bytes){
			DATA_->model.update(poll, [dev_id, dirty_marker, &bytes](model&& m){
				if (const auto ptr = m.devices.find(dev_id)) {
					if (ptr->service->dirty_marker.load() > dirty_marker) {
						// These bytes are already out of date
						return m;
					}
					auto dev = *ptr;
					dev.last_saved_state = bytes;
					dev.service->saved_marker = dirty_marker;
					m.devices = m.devices.insert(dev);
				}
				return m;
			});
		};
		save_async(poll, dev.id, with_bytes);
	}
}

static
auto save_dirty_device_states(poll_t) -> void {
	try {
		const auto m = DATA_->model.read(poll);
		for (const auto& dev : m.devices) {
			save_dirty_device_state(poll, dev);
		}
	} catch (const std::exception& err) {
		ui::send(ui::msg::error{std::format("save_dirty_device_states: {}", err.what())});
	}
}

[[nodiscard]] static
auto is_running(const sandbox& sbox) -> bool {
	return sbox.service && launched(sbox) && sbox.service->proc.running();
}

[[nodiscard]] static
auto is_running(id::sandbox sbox) -> bool {
	return is_running(scuff::DATA_->model.read(ez::nort).sandboxes.at({sbox}));
}

static
auto send_heartbeat(poll_t) -> void {
	const auto m = DATA_->model.read(poll);
	for (const auto& sbox : m.sandboxes) {
		if (is_running(sbox)) {
			sbox.service->enqueue(msg::in::heartbeat{});
		}
	}
}

static
auto poll_thread(std::stop_token stop_token) -> void {
	auto now     = std::chrono::steady_clock::now();
	auto next_gc = now + std::chrono::milliseconds{GC_INTERVAL_MS};
	auto next_hb = now + std::chrono::milliseconds{HEARTBEAT_INTERVAL_MS};
	auto next_dd = now + std::chrono::milliseconds{DIRTY_DEVICE_MS};
	while (!stop_token.stop_requested()) {
		now = std::chrono::steady_clock::now();
		if (now > next_gc) {
			DATA_->model.gc(ez::nort);
			next_gc = now + std::chrono::milliseconds{GC_INTERVAL_MS};
		}
		if (now > next_hb) {
			send_heartbeat(poll);
			next_hb = now + std::chrono::milliseconds{HEARTBEAT_INTERVAL_MS};
		}
		process_sandbox_messages(poll);
		if (now > next_dd) {
			save_dirty_device_states(poll);
			next_dd = now + std::chrono::milliseconds{DIRTY_DEVICE_MS};
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{POLL_SLEEP_MS});
	}
}

static
auto activate(ez::nort_t, id::group group_id, double sr) -> void {
	DATA_->model.update(ez::nort, [group_id, sr](model&& m){
		auto group = m.groups.at(group_id);
		group.flags.value |= group_flags::is_active;
		group.sample_rate = sr;
		for (const auto sbox_id : group.sandboxes) {
			const auto& sbox = m.sandboxes.at(sbox_id);
			sbox.service->enqueue(scuff::msg::in::activate{sr});
			sbox.service->enqueue(scuff::msg::in::set_render_mode{group.render_mode});
		}
		m.groups = m.groups.insert(group);
		return m;
	});
}

static
auto deactivate(ez::nort_t, id::group group_id) -> void {
	DATA_->model.update(ez::nort, [group_id](model&& m){
		auto group = m.groups.at(group_id);
		group.flags.value &= ~group_flags::is_active;
		m.groups = m.groups.insert(group);
		for (const auto sbox_id : group.sandboxes) {
			m.sandboxes.at(sbox_id).service->enqueue(scuff::msg::in::deactivate{});
		}
		return m;
	});
}

static
auto close_all_editors(ez::nort_t) -> void {
	const auto sandboxes = scuff::DATA_->model.read(ez::nort).sandboxes;
	for (const auto& sandbox : sandboxes) {
		if (is_running(sandbox)) {
			sandbox.service->enqueue(scuff::msg::in::close_all_editors{});
		}
	}
}

static
auto connect(ez::nort_t, id::device dev_out_id, size_t port_out, id::device dev_in_id, size_t port_in) -> void {
	DATA_->model.update_publish(ez::nort, [dev_out_id, port_out, dev_in_id, port_in](model&& m){
		const auto& dev_out = m.devices.at(dev_out_id);
		const auto& dev_in  = m.devices.at(dev_in_id);
		if (dev_out.sbox == dev_in.sbox) {
			// Devices are in the same sandbox
			const auto& sbox = m.sandboxes.at(dev_out.sbox);
			sbox.service->enqueue(scuff::msg::in::device_connect{dev_out_id.value, port_out, dev_in_id.value, port_in});
			return m;
		}
		// Devices are in different sandboxes
		const auto& sbox_out    = m.sandboxes.at(dev_out.sbox);
		const auto& sbox_in     = m.sandboxes.at(dev_in.sbox);
		const auto group_out_id = sbox_out.group;
		const auto group_in_id  = sbox_in.group;
		if (group_out_id != group_in_id) {
			throw std::runtime_error("Cannot connect devices that exist in different sandbox groups.");
		}
		auto group = m.groups.at(group_out_id);
		cross_sbox_connection csc;
		csc.in_dev_id  = dev_in_id;
		csc.in_port    = port_in;
		csc.out_dev_id = dev_out_id;
		csc.out_port   = port_out;
		group.cross_sbox_conns = group.cross_sbox_conns.insert(csc);
		m.groups = m.groups.insert(group);
		return m;
	});
}

[[nodiscard]] static
auto find(const model& m, ext::id::plugin plugin_id) -> id::plugin {
	for (const auto& plugin : m.plugins) {
		if (plugin.ext_id == plugin_id) {
			return plugin.id;
		}
	}
	return {};
}

[[nodiscard]] static
auto find(ez::nort_t, ext::id::plugin plugin_id) -> id::plugin {
	return find(DATA_->model.read(ez::nort), plugin_id);
}

[[nodiscard]] static
auto set_creation_callback(model&& m, id::device dev_id, return_create_device_result return_fn) -> model {
	auto device = m.devices.at(dev_id);
	device.creation_callback = return_fn;
	m.devices = m.devices.insert(device);
	return m;
}

[[nodiscard]] static
auto create_plugin_device_async(model&& m, id::device dev_id, const sandbox& sbox, const scuff::plugin& plugin, return_create_device_result return_fn) -> model {
	scuff::device dev;
	dev.id            = dev_id;
	dev.sbox          = {sbox.id};
	dev.plugin_ext_id = plugin.ext_id;
	dev.plugin        = plugin.id;
	dev.type          = plugin.type;
	dev.service       = std::make_shared<device_service>();
	m = scuff::add_device_to_sandbox(std::move(m), {sbox.id}, dev.id);
	m.devices = m.devices.insert(dev);
	// Plugin is available so we need to send a message to the sandbox to create the remote device.
	const auto callback = sbox.service->return_buffers.device_create_results.put(return_fn);
	const auto plugfile = m.plugfiles.at(plugin.plugfile);
	sbox.service->enqueue(msg::in::device_create{dev.id.value, plugin.type, plugfile.path, plugin.ext_id.value, callback});
	return m;
}

[[nodiscard]] static
auto create_unknown_plugin_device(model&& m, id::device dev_id, const sandbox& sbox, plugin_type type, scuff::ext::id::plugin plugin_ext_id, return_create_device_result return_fn) -> model {
	scuff::device dev;
	dev.id                = dev_id;
	dev.sbox              = {sbox.id};
	dev.plugin_ext_id     = plugin_ext_id;
	dev.type              = type;
	dev.service           = std::make_shared<device_service>();
	dev.error             = "Plugin not found.";
	dev.creation_callback = return_fn;
	m = scuff::add_device_to_sandbox(std::move(m), {sbox.id}, dev.id);
	m.devices = m.devices.insert(dev);
	return m;
}

[[nodiscard]] static
auto create_device_async(ez::nort_t, id::sandbox sbox_id, plugin_type type, ext::id::plugin plugin_ext_id, return_create_device_result return_fn) -> id::device {
	const auto dev_id = id::device{scuff::id_gen_++};
	DATA_->model.update(ez::nort, [dev_id, sbox_id, type, plugin_ext_id, return_fn](model&& m){
		const auto sbox      = m.sandboxes.at(sbox_id);
		const auto plugin_id = id::plugin{find(m, plugin_ext_id)};
		// The return callback will be called in the poll thread so this wrapper
		// is to pass it back to the main thread to be called there instead.
		const auto wrapper = [sbox, return_fn](create_device_result result) {
			ui::send(sbox, ui::msg::device_create{result, return_fn});
		};
		if (!plugin_id) {
			return create_unknown_plugin_device(std::move(m), dev_id, sbox, type, plugin_ext_id, wrapper);
		}
		return create_plugin_device_async(std::move(m), dev_id, sbox, m.plugins.at(plugin_id), wrapper);
	});
	return dev_id;
}

struct blocking_sandbox_operation {
	static constexpr auto MAX_WAIT = std::chrono::seconds(5);
	template <typename Fn> [[nodiscard]] auto make_fn(Fn fn) {
		return [this, fn](auto... args) {
			std::lock_guard lock{mutex_};
			fn(args...);
			cv_.notify_one();
		};
	}
	template <typename Pred>
	auto wait_for(Pred pred) -> bool {
		if (pred()) {
			return true;
		}
		auto lock = std::unique_lock{mutex_};
		return cv_.wait_for(lock, MAX_WAIT, pred);
	}
private:
	std::condition_variable cv_;
	std::mutex mutex_;
};

[[nodiscard]] static
auto create_device(ez::nort_t, id::sandbox sbox_id, plugin_type type, ext::id::plugin plugin_ext_id) -> create_device_result {
	auto m = DATA_->model.read(ez::nort);
	const auto dev_id    = id::device{scuff::id_gen_++};
	const auto plugin_id = id::plugin{find(m, plugin_ext_id)};
	const auto sbox      = m.sandboxes.at(sbox_id);
	if (!plugin_id) {
		DATA_->model.update(ez::nort, [dev_id, sbox, type, plugin_ext_id](model&& m) {
			return create_unknown_plugin_device(std::move(m), dev_id, sbox, type, plugin_ext_id, nullptr);
		});
		return create_device_result{dev_id, false};
	}
	std::optional<create_device_result> result;
	blocking_sandbox_operation bso;
	auto fn = bso.make_fn([&result](create_device_result remote_result) -> void {
		result = remote_result;
	});
	auto ready = [&result] {
		return result.has_value();
	};
	DATA_->model.update(ez::nort, [dev_id, sbox, plugin_id, fn](model&& m) {
		return create_plugin_device_async(std::move(m), dev_id, sbox, m.plugins.at(plugin_id), fn);
	});
	if (!bso.wait_for(ready)) {
		throw std::runtime_error("Timed out waiting for device creation.");
	}
	if (!result) {
		return create_device_result{dev_id, false};
	}
	return *result;
}

static
auto device_disconnect(ez::nort_t, id::device dev_out_id, size_t port_out, id::device dev_in_id, size_t port_in) -> void {
	DATA_->model.update_publish(ez::nort, [dev_out_id, port_out, dev_in_id, port_in](model&& m){
		const auto& dev_out = m.devices.at({dev_out_id});
		const auto& dev_in  = m.devices.at({dev_in_id});
		if (dev_out.sbox == dev_in.sbox) {
			// Devices are in the same sandbox.
			const auto& sbox = m.sandboxes.at(dev_out.sbox);
			sbox.service->enqueue(scuff::msg::in::device_disconnect{dev_out_id.value, port_out, dev_in_id.value, port_in});
			return m;
		}
		// Devices are in different sandboxes.
		const auto& sbox_out    = m.sandboxes.at(dev_out.sbox);
		const auto& sbox_in     = m.sandboxes.at(dev_in.sbox);
		const auto group_out_id = sbox_out.group;
		const auto group_in_id  = sbox_in.group;
		if (group_out_id != group_in_id) {
			throw std::runtime_error("Connected devices somehow exist in different sandbox groups?!");
		}
		auto group             = m.groups.at(group_out_id);
		cross_sbox_connection csc;
		csc.in_dev_id  = dev_in_id;
		csc.in_port    = port_in;
		csc.out_dev_id = dev_out_id;
		csc.out_port   = port_out;
		group.cross_sbox_conns = group.cross_sbox_conns.erase(csc);
		m.groups = m.groups.insert(group);
		return m;
	});
}

[[nodiscard]]
auto has_remote(const device& dev) -> bool {
	return dev.flags.value & client_device_flags::has_remote;
}

static
auto duplicate_async(ez::nort_t, id::device src_dev_id, id::sandbox dst_sbox_id, return_create_device_result return_fn, bool return_to_ui) -> id::device {
	auto m                   = DATA_->model.read(ez::nort);
	const auto new_dev_id    = id::device{scuff::id_gen_++};
	const auto src_dev       = m.devices.at({src_dev_id});
	const auto src_sbox      = m.sandboxes.at(src_dev.sbox);
	const auto dst_sbox      = m.sandboxes.at({dst_sbox_id});
	const auto plugin_ext_id = src_dev.plugin_ext_id;
	const auto plugin        = src_dev.plugin ? src_dev.plugin : find(ez::nort, plugin_ext_id);
	const auto type          = src_dev.type;
	if (return_to_ui) {
		return_fn = [dst_sbox, return_fn](create_device_result result) {
			ui::send(dst_sbox, ui::msg::device_create{result, return_fn});
		};
	}
	if (!plugin) {
		// Plugin isn't known yet.
		DATA_->model.update(ez::nort, [new_dev_id, dst_sbox, type, plugin_ext_id, return_fn](model&& m) {
			return create_unknown_plugin_device(std::move(m), new_dev_id, dst_sbox, type, plugin_ext_id, return_fn);
		});
		return new_dev_id;
	}
	if (!has_remote(src_dev)) {
		// Plugin is known but the source device hasn't been created remotely yet.
		DATA_->model.update(ez::nort, [new_dev_id, dst_sbox, src_dev_id, plugin, return_fn](model&& m) {
			return create_plugin_device_async(std::move(m), new_dev_id, dst_sbox, m.plugins.at(plugin), return_fn);
		});
		return new_dev_id;
	}
	// We're going to send a message to the source sandbox to save the source device.
	// When the saved state is returned, call this function with it:
	const auto save_cb = src_sbox.service->return_buffers.states.put([return_fn, new_dev_id, dst_sbox, plugin](const std::vector<std::byte>& src_state) mutable {
		// Now we're going to send a message to the destination sandbox to actually create the new device.
		// When the new device is created, call this function with it:
		auto wrapper = [fn = return_fn, dst_sbox, src_state](create_device_result result) {
			if (result.success) {
				// Remote device was created successfully.
				// Now send a message to the destination sandbox to load the saved state into the new device.
				dst_sbox.service->enqueue(msg::in::device_load{result.id.value, src_state});
			}
			// Call user's callback
			fn(result);
		};
		DATA_->model.update(ez::nort, [new_dev_id, dst_sbox, plugin, wrapper](model&& m){
			return create_plugin_device_async(std::move(m), new_dev_id, dst_sbox, m.plugins.at(plugin), wrapper);
		});
	});
	src_sbox.service->enqueue(msg::in::device_save{src_dev_id.value, save_cb});
	return new_dev_id;
}

static
auto duplicate(ez::nort_t, id::device src_dev_id, id::sandbox dst_sbox_id) -> create_device_result {
	std::optional<create_device_result> result;
	blocking_sandbox_operation bso;
	auto fn = bso.make_fn([&result](create_device_result value) -> void {
		result = value;
	});
	auto ready = [&result] {
		return result.has_value();
	};
	duplicate_async(ez::nort, src_dev_id, dst_sbox_id, fn, false);
	if (!bso.wait_for(ready)) {
		throw std::runtime_error("Timed out waiting for device duplication.");
	}
	return *result;
}

[[nodiscard]] static
auto find(ez::nort_t, id::device dev_id, ext::id::param param_id) -> idx::param {
	const auto m   = DATA_->model.read(ez::nort);
	const auto dev = m.devices.at(dev_id);
	for (size_t i = 0; i < dev.param_info.size(); i++) {
		const auto& info = dev.param_info[i];
		if (info.id == param_id) {
			return {i};
		}
	}
	return {};
}

[[nodiscard]] static
auto get_features(ez::nort_t, id::plugin plugin) -> std::vector<std::string> {
	const auto list = DATA_->model.read(ez::nort).plugins.at(plugin).clap_features;
	std::vector<std::string> out;
	for (const auto& feature : list) {
		out.push_back(feature);
	}
	return out;
}

[[nodiscard]] static
auto get_param_count(ez::nort_t, id::device dev) -> size_t {
	const auto m       = DATA_->model.read(ez::nort);
	const auto& device = m.devices.at(dev);
	return device.param_info.size();
}

static
auto get_param_value_text(ez::nort_t, id::device dev, idx::param param, double value, return_string fn) -> void {
	const auto m        = DATA_->model.read(ez::nort);
	const auto& device  = m.devices.at({dev});
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto callback = sbox.service->return_buffers.strings.put(fn);
	sbox.service->enqueue(msg::in::get_param_value_text{dev.value, param.value, value, callback});
}

[[nodiscard]] static
auto get_plugin(ez::nort_t, id::device dev) -> id::plugin {
	return DATA_->model.read(ez::nort).devices.at(dev).plugin;
}

[[nodiscard]] static
auto get_type(ez::nort_t, id::plugin id) -> plugin_type {
	return DATA_->model.read(ez::nort).plugins.at(id).type;
}

[[nodiscard]] static
auto has_gui(ez::nort_t, id::device dev) -> bool {
	const auto m       = DATA_->model.read(ez::nort);
	const auto& device = m.devices.at(dev);
	return device.flags.value & client_device_flags::has_gui;
}

[[nodiscard]] static
auto has_params(ez::nort_t, id::device dev) -> bool {
	const auto m       = DATA_->model.read(ez::nort);
	const auto& device = m.devices.at(dev);
	return device.flags.value & client_device_flags::has_params;
}

[[nodiscard]] static
auto has_rack_features(const immer::vector<std::string>& features) -> bool {
	for (const auto& feature : features) {
		if (feature == CLAP_PLUGIN_FEATURE_ANALYZER)     { return true; }
		if (feature == CLAP_PLUGIN_FEATURE_AUDIO_EFFECT) { return true; }
	}
	return false;
}

[[nodiscard]] static
auto has_rack_features(ez::nort_t, id::plugin id) -> bool {
	const auto m       = DATA_->model.read(ez::nort);
	const auto& plugin = m.plugins.at(id);
	switch (plugin.type) {
		case plugin_type::clap: {
			return has_rack_features(plugin.clap_features);
		}
		case plugin_type::vst3: {
			// Not implemented yet
			return false;
		}
		default: {
			return false;
		}
	}
}

static
auto set_render_mode(ez::nort_t, id::group group_id, render_mode mode) -> void {
	const auto m = DATA_->model.read(ez::nort);
	auto group   = m.groups.at(group_id);
	group.render_mode = mode;
	for (const auto sbox_id : group.sandboxes) {
		const auto& sbox = m.sandboxes.at(sbox_id);
		if (is_running(sbox)) {
			sbox.service->enqueue(scuff::msg::in::set_render_mode{mode});
		}
	}
	DATA_->model.update(ez::nort, [group](model&& m){
		m.groups = m.groups.insert(group);
		return m;
	});
}

static
auto set_track_color(ez::nort_t, id::device dev, std::optional<rgba32> color) -> void {
	const auto m = DATA_->model.read(ez::nort);
	const auto& device = m.devices.at(dev);
	const auto& sbox = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::set_track_color{dev.value, color});
}

static
auto set_track_name(ez::nort_t, id::device dev, std::string_view name) -> void {
	const auto m = DATA_->model.read(ez::nort);
	const auto& device = m.devices.at(dev);
	const auto& sbox = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::set_track_name{dev.value, std::string{name}});
}

[[nodiscard]] static
auto get_broken_plugfiles(ez::nort_t) -> std::vector<id::plugfile> {
	std::vector<id::plugfile> out;
	const auto m = DATA_->model.read(ez::nort);
	for (const auto& pf : m.plugfiles) {
		if (!pf.error->empty()) {
			out.push_back(pf.id);
		}
	}
	return out;
}

[[nodiscard]] static
auto get_broken_plugins(ez::nort_t) -> std::vector<id::plugin> {
	std::vector<id::plugin> out;
	const auto m = DATA_->model.read(ez::nort);
	for (const auto& plugin : m.plugins) {
		if (!plugin.error->empty()) {
			out.push_back(plugin.id);
		}
	}
	return out;
}

[[nodiscard]] static
auto get_devices(ez::nort_t, id::sandbox sbox_id) -> std::vector<id::device> {
	const auto m     = DATA_->model.read(ez::nort);
	const auto& sbox = m.sandboxes.at(sbox_id);
	std::vector<id::device> out;
	for (const auto dev_id : sbox.devices) {
		out.push_back(dev_id);
	}
	return out;
}

static
auto gui_hide(ez::nort_t, id::device dev) -> void {
	const auto m       = DATA_->model.read(ez::nort);
	const auto& device = m.devices.at(dev);
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::device_gui_hide{dev.value});
}

static
auto gui_show(ez::nort_t, id::device dev) -> void {
	const auto m       = DATA_->model.read(ez::nort);
	const auto& device = m.devices.at(dev);
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::device_gui_show{dev.value});
}

[[nodiscard]] static
auto was_created_successfully(ez::nort_t, id::device dev) -> bool {
	return DATA_->model.read(ez::nort).devices.at(dev).flags.value & client_device_flags::has_remote;
}

[[nodiscard]] static
auto create_group(ez::nort_t, void* parent_window_handle) -> id::group {
	const auto group_id = id::group{id_gen_++};
	DATA_->model.update(ez::nort, [group_id, parent_window_handle](model&& m){
		scuff::group group;
		group.id                   = group_id;
		group.parent_window_handle = parent_window_handle;
		const auto shmid    = shm::make_group_id(DATA_->instance_id, group.id);
		group.service      = std::make_shared<group_service>();
		group.service->shm = shm::create_group(shmid, true);
		group.service->signaler.local = &group.service->shm.signaling;
		group.service->signaler.shm   = &group.service->shm.data->signaling;
		m.groups = m.groups.insert(group);
		return m;
	});
	return group_id;
}

static
auto is_scanning(ez::safe_t) -> bool {
	return DATA_->scanning;
}

static
auto get_value_async(ez::nort_t, id::device dev_id, idx::param param, return_double fn) -> void {
	const auto& m       = DATA_->model.read(ez::nort);
	const auto& device  = m.devices.at(dev_id);
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto wrapper = [sbox, fn](double value) -> void {
		ui::send(sbox, ui::msg::return_param_value{value, fn});
	};
	const auto callback = sbox.service->return_buffers.doubles.put(wrapper);
	sbox.service->enqueue(scuff::msg::in::get_param_value{dev_id.value, param.value, callback});
}

static
auto get_value(ez::nort_t, id::device dev_id, idx::param param) -> double {
	std::optional<double> result;
	blocking_sandbox_operation bso;
	auto fn = bso.make_fn([&result](double value) -> void {
		result = value;
	});
	auto ready = [&result] {
		return result.has_value();
	};
	const auto& m       = DATA_->model.read(ez::nort);
	const auto& device  = m.devices.at(dev_id);
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto callback = sbox.service->return_buffers.doubles.put(fn);
	sbox.service->enqueue(scuff::msg::in::get_param_value{dev_id.value, param.value, callback});
	if (!bso.wait_for(ready)) {
		throw std::runtime_error("Timed out waiting for value.");
	}
	return *result;
}

[[nodiscard]] static
auto get_port_info(ez::nort_t, id::device dev_id) -> device_port_info {
	return DATA_->model.read(ez::nort).devices.at(dev_id).port_info;
}

[[nodiscard]] static
auto get_info(ez::nort_t, id::device dev_id, idx::param param) -> client_param_info {
	const auto m   = DATA_->model.read(ez::nort);
	const auto dev = m.devices.at(dev_id);
	if (param.value >= dev.param_info.size()) {
		throw std::runtime_error("Invalid parameter index.");
	}
	return dev.param_info[param.value];
}

static
auto push_event(ez::nort_t, id::device dev, const scuff::event& event) -> void {
	const auto m       = DATA_->model.read(ez::nort);
	const auto& device = m.devices.at({dev});
	const auto& sbox   = m.sandboxes.at(device.sbox);
	intercept_input_event(ez::nort, device, event);
	sbox.service->enqueue(scuff::msg::in::event{dev.value, event});
}

[[nodiscard]] static
auto get_path(ez::nort_t, id::plugfile plugfile) -> std::string_view {
	return *DATA_->model.read(ez::nort).plugfiles.at({plugfile}).path;
}

[[nodiscard]] static
auto get_plugfile(ez::nort_t, id::plugin plugin) -> id::plugfile {
	return DATA_->model.read(ez::nort).plugins.at({plugin}).plugfile;
}

[[nodiscard]] static
auto get_error(ez::nort_t, id::device dev) -> std::string_view {
	return *DATA_->model.read(ez::nort).devices.at(dev).error;
}

[[nodiscard]] static
auto get_error(ez::nort_t, id::plugfile plugfile) -> std::string_view {
	return *DATA_->model.read(ez::nort).plugfiles.at({plugfile}).error;
}

[[nodiscard]] static
auto get_error(ez::nort_t, id::plugin plugin) -> std::string_view {
	return *DATA_->model.read(ez::nort).plugins.at({plugin}).error;
}

[[nodiscard]] static
auto get_ext_id(ez::nort_t, id::plugin plugin) -> ext::id::plugin {
	return DATA_->model.read(ez::nort).plugins.at(plugin).ext_id;
}

[[nodiscard]] static
auto get_name(ez::nort_t, id::plugin plugin) -> std::string_view {
	return *DATA_->model.read(ez::nort).plugins.at({plugin}).name;
}

[[nodiscard]] static
auto get_plugin_ext_id(ez::nort_t, id::device dev) -> ext::id::plugin {
	return DATA_->model.read(ez::nort).devices.at(dev).plugin_ext_id;
}

static
auto get_value_text_async(ez::nort_t, id::device dev_id, idx::param param, double value, return_string fn) -> void {
	const auto m     = DATA_->model.read(ez::nort);
	const auto& dev  = m.devices.at(dev_id);
	const auto& sbox = m.sandboxes.at(dev.sbox);
	const auto wrapper = [sbox, fn](std::string_view text) -> void {
		ui::send(sbox, ui::msg::return_param_value_text{std::string{text}, fn});
	};
	const auto callback = sbox.service->return_buffers.strings.put(wrapper);
	sbox.service->enqueue(scuff::msg::in::get_param_value_text{dev_id.value, param.value, value, callback});
}

[[nodiscard]] static
auto get_value_text(ez::nort_t, id::device dev_id, idx::param param, double value) -> std::string {
	std::string result;
	blocking_sandbox_operation bso;
	auto fn = bso.make_fn([&result](std::string_view text) -> void {
		result = text;
	});
	auto ready = [&result] {
		return !result.empty();
	};
	const auto m     = DATA_->model.read(ez::nort);
	const auto& dev  = m.devices.at(dev_id);
	const auto& sbox = m.sandboxes.at(dev.sbox);
	const auto callback = sbox.service->return_buffers.strings.put(fn);
	sbox.service->enqueue(scuff::msg::in::get_param_value_text{dev_id.value, param.value, value, callback});
	if (!bso.wait_for(ready)) {
		throw std::runtime_error("Timed out waiting for value text.");
	}
	return result;
}

[[nodiscard]] static
auto get_vendor(ez::nort_t, id::plugin plugin) -> std::string_view {
	return *DATA_->model.read(ez::nort).plugins.at({plugin}).vendor;
}

[[nodiscard]] static
auto get_version(ez::nort_t, id::plugin plugin) -> std::string_view {
	return *DATA_->model.read(ez::nort).plugins.at({plugin}).version;
}

static
auto load_async(ez::nort_t, const model& m, const scuff::device& dev, const scuff::bytes& state, return_load_device_result fn) -> void {
	const auto sbox = m.sandboxes.at(dev.sbox);
	sbox.service->enqueue(msg::in::device_load{dev.id.value, state, sbox.service->return_buffers.device_load_results.put(fn)});
}

static
auto load_async(ez::nort_t, id::device dev_id, const scuff::bytes& state, return_load_device_result fn) -> void {
	const auto m   = DATA_->model.read(ez::nort);
	const auto& dev = m.devices.at(dev_id);
	load_async(ez::nort, m, dev, state, fn);
}

static
auto restart(ez::nort_t, id::sandbox sbox, std::string_view sbox_exe_path) -> void {
	const auto m      = DATA_->model.read(ez::nort);
	auto sandbox      = m.sandboxes.at({sbox});
	const auto& group = m.groups.at(sandbox.group);
	if (sandbox.service->proc.running()) {
		sandbox.service->proc.terminate();
	}
	const auto group_shmid   = group.service->shm.seg.id;
	const auto sandbox_shmid = sandbox.service->get_shmid();
	const auto exe_args      = make_sbox_exe_args(std::to_string(os::get_process_id()), group_shmid, sandbox_shmid, reinterpret_cast<uint64_t>(group.parent_window_handle));
	sandbox.service->proc   = bp::child{std::string{sbox_exe_path}, exe_args};
	sandbox.flags.value     |= sandbox_flags::launched;
	for (const auto dev_id : sandbox.devices) {
		const auto& dev = m.devices.at(dev_id);
		const auto with_created_device = [m, dev](create_device_result result){
			const auto sbox = m.sandboxes.at(dev.sbox);
			ui::send(sbox, ui::msg::device_late_create{result});
			if (result.success) {
				load_async(ez::nort, m, dev, dev.last_saved_state, [](load_device_result){});
			}
			else {
				ui::send(ui::msg::error{std::format("Failed to restore device {} after sandbox restart.", dev.id.value)});
			}
		};
		const auto callback = sandbox.service->return_buffers.device_create_results.put(with_created_device);
		const auto plugin   = m.plugins.at(dev.plugin);
		const auto plugfile = m.plugfiles.at(plugin.plugfile);
		sandbox.service->enqueue(msg::in::device_create{dev.id.value, dev.type, plugfile.path, dev.plugin_ext_id.value, callback});
	}
	sandbox.service->enqueue(msg::in::activate{group.sample_rate});
	sandbox.service->enqueue(msg::in::set_render_mode{group.render_mode});
	DATA_->model.update(ez::nort, [sandbox](model&& m){
		m.sandboxes = m.sandboxes.insert(sandbox);
		return m;
	});
}

[[nodiscard]] static
auto load(ez::nort_t, id::device dev, const scuff::bytes& bytes) -> bool {
	bool done    = false;
	bool success = false;
	blocking_sandbox_operation bso;
	auto fn = bso.make_fn([&done, &success](load_device_result result) -> void {
		done    = true;
		success = result.success;
	});
	auto ready = [&done] {
		return done;
	};
	load_async(ez::nort, dev, bytes, fn);
	if (!bso.wait_for(ready)) {
		throw std::runtime_error("Timed out waiting for device load.");
	}
	return success;
}

[[nodiscard]] static
auto save(ez::nort_t, id::device dev_id) -> scuff::bytes {
	scuff::bytes bytes;
	bool done = false;
	blocking_sandbox_operation bso;
	auto fn = bso.make_fn([&bytes, &done](const scuff::bytes& b) -> void {
		bytes = b;
		done  = true;
	});
	auto ready = [&done] {
		return done;
	};
	const auto m    = DATA_->model.read(ez::nort);
	const auto& dev = m.devices.at(dev_id);
	const auto sbox = m.sandboxes.at(dev.sbox);
	auto wrapper_fn = [dev_id = dev.id, sbox, fn](const scuff::bytes& bytes){
		update_saved_state_with_returned_bytes(ez::nort, dev_id, bytes);
		fn(bytes);
	};
	sbox.service->enqueue(msg::in::device_save{dev.id.value, sbox.service->return_buffers.states.put(wrapper_fn)});
	if (!bso.wait_for(ready)) {
		throw std::runtime_error("Timed out waiting for device save.");
	}
	return bytes;
}

static
auto do_scan(ez::nort_t, std::string_view scan_exe_path, scan_flags flags) -> void {
	scan_::stop_if_it_is_already_running();
	scan_::start(scan_exe_path, flags);
}

[[nodiscard]] static
auto add_sandbox_to_group(model m, id::group group, id::sandbox sbox) -> model {
	m.groups = m.groups.update_if_exists(group, [m, sbox](scuff::group g) {
		g.sandboxes              = g.sandboxes.insert(sbox);
		g.total_active_sandboxes = get_active_sandbox_count(m, g);
		return g;
	});
	return m;
}

[[nodiscard]] static
auto remove_sandbox_from_group(model&& m, id::group group, id::sandbox sbox) -> model {
	m.groups = m.groups.update_if_exists(group, [m, sbox](scuff::group g) {
		g.sandboxes              = g.sandboxes.erase(sbox);
		g.total_active_sandboxes = get_active_sandbox_count(m, g);
		return g;
	});
	return m;
}

[[nodiscard]] static
auto create_sandbox(ez::nort_t, id::group group_id, std::string_view sbox_exe_path) -> id::sandbox {
	const auto sbox_id = id::sandbox{id_gen_++};
	DATA_->model.update_publish(ez::nort, [=](model&& m){
		sandbox sbox;
		sbox.id = sbox_id;
		const auto& group        = m.groups.at({group_id});
		const auto group_shmid   = group.service->shm.seg.id;
		const auto sandbox_shmid = shm::make_sandbox_id(DATA_->instance_id, sbox.id);
		const auto exe_args      = make_sbox_exe_args(std::to_string(os::get_process_id()), group_shmid, sandbox_shmid, reinterpret_cast<uint64_t>(group.parent_window_handle));
		auto proc                = bp::child{std::string{sbox_exe_path}, exe_args};
		if (!proc.running()) {
			throw std::runtime_error("Failed to launch sandbox process.");
		}
		sbox.flags.value        |= sandbox_flags::launched;
		sbox.group               = {group_id};
		sbox.service            = std::make_shared<sandbox_service>(std::move(proc), sandbox_shmid);
		m.sandboxes              = m.sandboxes.insert(sbox);
		m = add_sandbox_to_group(m, {group_id}, sbox.id);
		m.sandboxes = m.sandboxes.insert(sbox);
		return m;
	});
	return sbox_id;
}

[[nodiscard]] static auto is_marked_for_delete(const group& g) -> bool { return g.flags.value & group_flags::marked_for_delete; } 
[[nodiscard]] static auto is_marked_for_delete(const sandbox& s) -> bool { return s.flags.value & sandbox_flags::marked_for_delete; } 
[[nodiscard]] static auto is_marked_for_delete(const model& m, id::group group_id) -> bool { return m.groups.at(group_id).flags.value & group_flags::marked_for_delete; } 
[[nodiscard]] static auto is_marked_for_delete(const model& m, id::sandbox sbox_id) -> bool { return m.sandboxes.at(sbox_id).flags.value & sandbox_flags::marked_for_delete; } 
[[nodiscard]] static auto is_ready_to_erase(const group& g) -> bool { return is_marked_for_delete(g) && g.sandboxes.empty(); } 
[[nodiscard]] static auto is_ready_to_erase(const sandbox& s) -> bool { return is_marked_for_delete(s) && s.devices.empty(); } 
[[nodiscard]] static auto is_ready_to_erase(const model& m, id::group group_id) -> bool { return is_ready_to_erase(m.groups.at(group_id)); } 
[[nodiscard]] static auto is_ready_to_erase(const model& m, id::sandbox sbox_id) -> bool { return is_ready_to_erase(m.sandboxes.at(sbox_id)); } 
[[nodiscard]] static auto mark_for_delete(group&& g) -> group { g.flags.value |= group_flags::marked_for_delete; return g; }
[[nodiscard]] static auto mark_for_delete(sandbox&& s) -> sandbox { s.flags.value |= sandbox_flags::marked_for_delete; return s; }

[[nodiscard]] static
auto mark_for_delete(model&& m, id::group group_id) -> model {
	auto group = m.groups.at(group_id);
	group = mark_for_delete(std::move(group));
	m.groups = m.groups.insert(group);
	return m;
}

[[nodiscard]] static
auto mark_for_delete(model&& m, id::sandbox sbox_id) -> model {
	auto sbox = m.sandboxes.at(sbox_id);
	sbox = mark_for_delete(std::move(sbox));
	m.sandboxes = m.sandboxes.insert(sbox);
	return m;
}

[[nodiscard]] static
auto actually_erase(model&& m, id::group group_id) -> model {
	m.groups = m.groups.erase(group_id);
	return m;
}

[[nodiscard]] static
auto actually_erase(model&& m, id::sandbox sbox_id) -> model {
	const auto sbox  = m.sandboxes.at(sbox_id);
	m = remove_sandbox_from_group(std::move(m), sbox.group, sbox_id);
	m.sandboxes = m.sandboxes.erase(sbox_id);
	const auto group = m.groups.at(sbox.group);
	if (is_ready_to_erase(group)) {
		m = actually_erase(std::move(m), group.id);
	}
	return m;
}

[[nodiscard]] static
auto actually_erase(model&& m, id::device dev_id) -> model {
	const auto dev = m.devices.at(dev_id);
	m = remove_device_from_sandbox(std::move(m), dev.sbox, dev_id);
	m.devices = m.devices.erase(dev_id);
	auto sbox = m.sandboxes.at(dev.sbox);
	if (is_ready_to_erase(sbox)) {
		m = actually_erase(std::move(m), sbox.id);
	}
	return m;
}

[[nodiscard]] static
auto erase(model&& m, id::group group_id) -> model {
	auto group = m.groups.at(group_id);
	if (group.sandboxes.empty()) { return actually_erase(std::move(m), group_id); }
	else                         { return mark_for_delete(std::move(m), group_id); }
}

[[nodiscard]] static
auto erase(model&& m, id::sandbox sbox_id) -> model {
	auto sbox = m.sandboxes.at(sbox_id);
	if (sbox.devices.empty()) { return actually_erase(std::move(m), sbox_id); }
	else                      { return mark_for_delete(std::move(m), sbox_id); }
}

[[nodiscard]] static
auto erase(model&& m, id::device dev_id) -> model {
	return actually_erase(std::move(m), dev_id);
}

static auto erase(ez::nort_t, id::group group_id) -> void  { DATA_->model.update_publish(ez::nort, [group_id](model&& m){ return erase(std::move(m), group_id); }); } 
static auto erase(ez::nort_t, id::sandbox sbox_id) -> void { DATA_->model.update_publish(ez::nort, [sbox_id](model&& m){ return erase(std::move(m), sbox_id); }); } 
static auto erase(ez::nort_t, id::device dev_id) -> void   { DATA_->model.update_publish(ez::nort, [dev_id](model&& m){ return erase(std::move(m), dev_id); }); }

[[nodiscard]] static
auto get_working_plugins(ez::nort_t) -> std::vector<id::plugin> {
	std::vector<id::plugin> out;
	const auto m = DATA_->model.read(ez::nort);
	for (const auto& plugin : m.plugins) {
		if (plugin.error->empty()) {
			out.push_back(plugin.id);
		}
	}
	return out;
}

auto ref(ez::nort_t, id::device id) -> void {
	DATA_->model.read(ez::nort).devices.at(id).service->ref_count++;
}

auto ref(ez::nort_t, id::group id) -> void {
	DATA_->model.read(ez::nort).groups.at(id).service->ref_count++;
}

auto ref(ez::nort_t, id::sandbox id) -> void {
	DATA_->model.read(ez::nort).sandboxes.at(id).service->ref_count++;
}

auto managed(ez::nort_t, id::device id) -> managed_device {
	ref(ez::nort, id);
	return managed_device{id};
}

auto managed(ez::nort_t, id::group id) -> managed_group {
	ref(ez::nort, id);
	return managed_group{id};
}

auto managed(ez::nort_t, id::sandbox id) -> managed_sandbox {
	ref(ez::nort, id);
	return managed_sandbox{id};
}

auto unref(ez::nort_t, id::device id) -> void {
	if (!DATA_) { return; }
	if (--DATA_->model.read(ez::nort).devices.at(id).service->ref_count <= 0) {
		erase(ez::nort, id);
	}
}

auto unref(ez::nort_t, id::group id) -> void {
	if (!DATA_) { return; }
	if (--DATA_->model.read(ez::nort).groups.at(id).service->ref_count <= 0) {
		erase(ez::nort, id);
	}
}

auto unref(ez::nort_t, id::sandbox id) -> void {
	if (!DATA_) { return; }
	if (--DATA_->model.read(ez::nort).sandboxes.at(id).service->ref_count <= 0) {
		erase(ez::nort, id);
	}
}

auto make_shm_emulation_process_folder() -> void {
	fs::create_directories(shm::get_shm_emulation_process_dir(sago::getDataHome(), std::to_string(os::get_process_id())));
}

auto cleanup_shm_emulation_folders() -> void {
	try {
		const auto root_dir = shm::get_shm_emulation_root_dir(sago::getDataHome());
		const auto proc_dir = shm::get_shm_emulation_process_dir(sago::getDataHome(), std::to_string(os::get_process_id()));
		for (const auto& entry : fs::directory_iterator(root_dir)) {
			if (entry.is_directory() && entry.path() != proc_dir) {
				const auto pid_str = entry.path().filename().string();
				if (!os::process_is_running(std::stoi(pid_str))) {
					fs::remove_all(entry.path());
				}
			}
		}
	}
	catch (...) {}
}

auto init() -> void {
	// TOODOO: figure out if we need to handle cleanup of old shm emulation files
	if (scuff::initialized_) { return; }
	try {
		scuff::DATA_               = std::make_unique<scuff::data>();
		scuff::DATA_->instance_id  = "scuff+" + std::to_string(scuff::os::get_process_id());
		scuff::DATA_->poll_thread  = std::jthread{impl::poll_thread};
		scuff::DATA_->ui_thread_id = std::this_thread::get_id();
		make_shm_emulation_process_folder();
		cleanup_shm_emulation_folders();
		scuff::initialized_        = true;
	} catch (const std::exception& err) {
		scuff::DATA_.reset();
		throw err;
	} catch (...) {
		scuff::DATA_.reset();
		throw std::runtime_error("Unknown error during initialization");
	}
}

auto shutdown() -> void {
	if (!scuff::initialized_) { return; }
	scuff::DATA_->poll_thread.request_stop();
	scuff::DATA_->scan_thread.request_stop();
	if (scuff::DATA_->poll_thread.joinable()) {
		scuff::DATA_->poll_thread.join();
	}
	if (scuff::DATA_->scan_thread.joinable()) {
		scuff::DATA_->scan_thread.join();
	}
	scuff::DATA_.reset();
	scuff::initialized_ = false;
}

} // impl

namespace scuff {

auto audio_process(const group_process& process) -> void {
	const auto audio = scuff::DATA_->model.read(ez::audio);
	if (const auto group = audio->groups.find({process.group})) {
		impl::process_inputs(ez::audio, *audio, process.audio_inputs, process.input_events);
		if (impl::do_sandbox_processing(ez::audio, audio, *group)) {
			impl::process_outputs(ez::audio, *audio, *group, process.audio_outputs, process.output_events);
		}
		else {
			impl::read_zeros(ez::audio, *audio, process.audio_outputs);
		}
	}
}

[[nodiscard]] static
auto api_error(std::string_view what, const std::source_location& location = std::source_location::current()) -> ui::msg::error {
	return ui::msg::error{std::format("Scuff API error in {}: {}", location.function_name(), what)};
}

auto init() -> void {
	try { impl::init(); } SCUFF_EXCEPTION_WRAPPER;
}

auto shutdown() -> void {
	try { impl::shutdown(); } SCUFF_EXCEPTION_WRAPPER;
}

auto activate(id::group group, double sr) -> void {
	try { impl::activate(ez::nort, group, sr); } SCUFF_EXCEPTION_WRAPPER;
}

auto close_all_editors() -> void {
	try { impl::close_all_editors(ez::nort); } SCUFF_EXCEPTION_WRAPPER;
}

auto connect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void {
	try { impl::connect(ez::nort, dev_out, port_out, dev_in, port_in); } SCUFF_EXCEPTION_WRAPPER;
}

auto create_device(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id) -> create_device_result {
	try { return impl::create_device(ez::nort, sbox, type, plugin_id); } SCUFF_EXCEPTION_WRAPPER;
}

auto create_device_async(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id, return_create_device_result fn) -> id::device {
	try { return impl::create_device_async(ez::nort, sbox, type, plugin_id, fn); } SCUFF_EXCEPTION_WRAPPER;
}

auto create_sandbox(id::group group_id, std::string_view sbox_exe_path) -> id::sandbox {
	try { return impl::create_sandbox(ez::nort, group_id, sbox_exe_path); } SCUFF_EXCEPTION_WRAPPER;
}

auto deactivate(id::group group) -> void {
	try { impl::deactivate(ez::nort, group); } SCUFF_EXCEPTION_WRAPPER;
}

auto disconnect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void {
	try { impl::device_disconnect(ez::nort, dev_out, port_out, dev_in, port_in); } SCUFF_EXCEPTION_WRAPPER;
}

auto duplicate(id::device dev, id::sandbox sbox) -> create_device_result {
	try { return impl::duplicate(ez::nort, dev, sbox); } SCUFF_EXCEPTION_WRAPPER;
}

auto duplicate_async(id::device dev, id::sandbox sbox, return_create_device_result fn) -> id::device {
	try { return impl::duplicate_async(ez::nort, dev, sbox, fn, true); } SCUFF_EXCEPTION_WRAPPER;
}

auto erase(id::device dev) -> void {
	try { impl::erase(ez::nort, {dev}); } SCUFF_EXCEPTION_WRAPPER;
}

auto erase(id::sandbox sbox) -> void {
	try { impl::erase(ez::nort, sbox); } SCUFF_EXCEPTION_WRAPPER;
}

auto find(id::device dev, ext::id::param param_id) -> idx::param {
	try { return impl::find(ez::nort, dev, param_id); } SCUFF_EXCEPTION_WRAPPER;
}

auto find(ext::id::plugin plugin_id) -> id::plugin {
	try { return impl::find(ez::nort, plugin_id); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_param_count(id::device dev) -> size_t {
	try { return impl::get_param_count(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_plugin(id::device dev) -> id::plugin {
	try { return impl::get_plugin(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto gui_hide(id::device dev) -> void {
	try { impl::gui_hide(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto gui_show(id::device dev) -> void {
	try { impl::gui_show(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto create_group(void* parent_window_handle) -> id::group {
	try { return impl::create_group(ez::nort, parent_window_handle); } SCUFF_EXCEPTION_WRAPPER;
}

auto erase(id::group group) -> void {
	try { impl::erase(ez::nort, {group}); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_broken_plugfiles() -> std::vector<id::plugfile> {
	try { return impl::get_broken_plugfiles(ez::nort); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_broken_plugins() -> std::vector<id::plugin> {
	try { return impl::get_broken_plugins(ez::nort); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_devices(id::sandbox sbox) -> std::vector<id::device> {
	try { return impl::get_devices(ez::nort, sbox); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_error(id::device device) -> std::string_view {
	try { return impl::get_error(ez::nort, device); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_error(id::plugfile plugfile) -> std::string_view {
	try { return impl::get_error(ez::nort, plugfile); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_error(id::plugin plugin) -> std::string_view {
	try { return impl::get_error(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_ext_id(id::plugin plugin) -> ext::id::plugin {
	try { return impl::get_ext_id(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_features(id::plugin plugin) -> std::vector<std::string> {
	try { return impl::get_features(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_port_info(id::device dev) -> device_port_info {
	try { return impl::get_port_info(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_info(id::device dev, idx::param param) -> client_param_info {
	try { return impl::get_info(ez::nort, dev, param); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_name(id::plugin plugin) -> std::string_view {
	try { return impl::get_name(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_path(id::plugfile plugfile) -> std::string_view {
	try { return impl::get_path(ez::nort, plugfile); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_plugfile(id::plugin plugin) -> id::plugfile {
	try { return impl::get_plugfile(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_plugin_ext_id(id::device dev) -> ext::id::plugin {
	try { return impl::get_plugin_ext_id(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_type(id::plugin plugin) -> plugin_type {
	try { return impl::get_type(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_value(id::device dev, idx::param param) -> double {
	try { return impl::get_value(ez::nort, dev, param); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_value_async(id::device dev, idx::param param, return_double fn) -> void {
	try { impl::get_value_async(ez::nort, dev, param, fn); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_value_text(id::device dev, idx::param param, double value) -> std::string {
	try { return impl::get_value_text(ez::nort, dev, param, value); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_value_text_async(id::device dev, idx::param param, double value, return_string fn) -> void {
	try { impl::get_value_text_async(ez::nort, dev, param, value, fn); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_vendor(id::plugin plugin) -> std::string_view {
	try { return impl::get_vendor(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_version(id::plugin plugin) -> std::string_view {
	try { return impl::get_version(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto get_working_plugins() -> std::vector<id::plugin> {
	try { return impl::get_working_plugins(ez::nort); } SCUFF_EXCEPTION_WRAPPER;
}

auto has_gui(id::device dev) -> bool {
	try { return impl::has_gui(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto has_params(id::device dev) -> bool {
	try { return impl::has_params(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto has_rack_features(id::plugin plugin) -> bool {
	try { return impl::has_rack_features(ez::nort, plugin); } SCUFF_EXCEPTION_WRAPPER;
}

auto is_running(id::sandbox sbox) -> bool {
	try { return impl::is_running(sbox); } SCUFF_EXCEPTION_WRAPPER;
}

auto is_scanning() -> bool {
	try { return impl::is_scanning(ez::nort); } SCUFF_EXCEPTION_WRAPPER;
}

auto load(id::device dev, const scuff::bytes& bytes) -> bool {
	try { return impl::load(ez::nort, dev, bytes); } SCUFF_EXCEPTION_WRAPPER;
}

auto load_async(id::device dev, const scuff::bytes& bytes, return_load_device_result fn) -> void {
	try { impl::load_async(ez::nort, dev, bytes, fn); } SCUFF_EXCEPTION_WRAPPER;
}

auto push_event(id::device dev, const scuff::event& event) -> void {
	try { impl::push_event(ez::nort, dev, event); } SCUFF_EXCEPTION_WRAPPER;
}

auto ui_update(const general_ui& ui) -> void {
	try { ui::call_callbacks(ui); } SCUFF_EXCEPTION_WRAPPER;
}

auto ui_update(id::group group_id, const group_ui& ui) -> void {
	try { ui::call_callbacks(group_id, ui); } SCUFF_EXCEPTION_WRAPPER;
}

auto restart(id::sandbox sbox, std::string_view sbox_exe_path) -> bool {
	try { impl::restart(ez::nort, sbox, sbox_exe_path); return true; } SCUFF_EXCEPTION_WRAPPER;
}

auto save(id::device dev) -> scuff::bytes {
	try { return impl::save(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto save_async(id::device dev, return_bytes fn) -> void {
	try { impl::save_async(ez::nort, dev, fn); } SCUFF_EXCEPTION_WRAPPER;
}

auto scan(std::string_view scan_exe_path, scan_flags flags) -> void {
	try { impl::do_scan(ez::nort, scan_exe_path, flags); } SCUFF_EXCEPTION_WRAPPER;
}

auto set_render_mode(id::group group, render_mode mode) -> void {
	try { impl::set_render_mode(ez::nort, group, mode); } SCUFF_EXCEPTION_WRAPPER;
}

auto set_track_color(id::device dev, std::optional<rgba32> color) -> void {
	try { impl::set_track_color(ez::nort, dev, color); } SCUFF_EXCEPTION_WRAPPER;
}

auto set_track_name(id::device dev, std::string_view name) -> void {
	try { impl::set_track_name(ez::nort, dev, name); } SCUFF_EXCEPTION_WRAPPER;
}

auto was_created_successfully(id::device dev) -> bool {
	try { return impl::was_created_successfully(ez::nort, dev); } SCUFF_EXCEPTION_WRAPPER;
}

auto managed(id::device id) -> managed_device {
	try { return impl::managed(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER;
}

auto managed(id::group id) -> managed_group {
	try { return impl::managed(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER;
}

auto managed(id::sandbox id) -> managed_sandbox {
	try { return impl::managed(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER;
}

auto ref(id::device id) -> void {
	try { impl::ref(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER;
}

auto ref(id::group id) -> void {
	try { impl::ref(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER;
}

auto ref(id::sandbox id) -> void {
	try { impl::ref(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER;
}

auto unref(id::device id) -> void {
	try { impl::unref(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER;
}

auto unref(id::group id) -> void {
	try { impl::unref(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER;
}

auto unref(id::sandbox id) -> void {
	try { impl::unref(ez::nort, id); } SCUFF_EXCEPTION_WRAPPER
}

} // scuff

namespace boost::interprocess::ipcdetail {

auto get_shared_dir(std::string& shared_dir) -> void  {
	shared_dir = scuff::shm::get_shm_emulation_process_dir(sago::getDataHome(), std::to_string(scuff::os::get_process_id())).string();
} 

auto get_shared_dir(std::wstring& shared_dir) -> void {
	shared_dir = scuff::shm::get_shm_emulation_process_dir(sago::getDataHome(), std::to_string(scuff::os::get_process_id())).wstring();
} 

} // namespace boost::interprocess::ipcdetail
