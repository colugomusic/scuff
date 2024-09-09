#include "common/speen.hpp"
#include "common/types.hpp"
#include "common/visit.hpp"
#include "scan.hpp"
#include <mutex>
#include <readerwriterqueue.h>
#include <string>
#include <variant>

namespace bip  = boost::interprocess;

namespace scuff {

static
auto publish(model m) -> void {
	DATA_->published_model.set(std::move(m));
}

template <typename EventT> [[nodiscard]] static
auto convert(const scuff_event_header& header) -> const EventT& {
	return *reinterpret_cast<const EventT*>(&header);
}

[[nodiscard]] static
auto convert(const scuff_event_header& header) -> scuff::events::event {
	switch (header.type) {
		case scuff_event_type_param_gesture_begin: { return convert<scuff_event_param_gesture_begin>(header); }
		case scuff_event_type_param_gesture_end:   { return convert<scuff_event_param_gesture_end>(header); }
		case scuff_event_type_param_value:         { return convert<scuff_event_param_value>(header); }
	}
	assert (false && "Invalid event header");
	return {};
}

[[nodiscard]] static
auto convert(const scuff::events::event& event) -> const scuff_event_header& {
	return fast_visit([](const auto& event) -> const scuff_event_header& { return event.header; }, event);
}

static
auto send(const sandbox& sbox, const scuff::msg::in::msg& msg) -> void {
	sbox.external->msg_queue.lock()->push_back(msg);
}

[[nodiscard]] static
auto make_sbox_exe_args(std::string_view group_id, std::string_view sandbox_id) -> std::vector<std::string> {
	std::vector<std::string> args;
	args.push_back(std::string("--group ") + group_id.data());
	args.push_back(std::string("--sandbox ") + sandbox_id.data());
	return args;
}

[[nodiscard]] static
auto wait_for_output_ready(const scuff::group& group, size_t frontside) -> bool {
	auto ready = [data = group.external->shm.data, frontside]() -> bool {
		return data->sandboxes_processing.value[frontside].load(std::memory_order_acquire) < 1;
	};
	if (ready()) {
		return true;
	}
	// Spin-wait until the sandboxes have finished processing.
	// If a sandbox is misbehaving then this might time out
	// and the buffer would be missed.
	auto success = speen::wait_for_a_bit(ready);
	assert (success && "Sandbox timed out");
	return false;
}

static
auto write_audio(const scuff::device& dev, const scuff_audio_writers& writers, size_t backside) -> void {
	for (size_t j = 0; j < writers.count; j++) {
		const auto& writer = writers.writers[j];
		auto& ab           = dev.external->shm_audio_ports.input_buffers[writer.port_index];
		auto& buffer       = ab.value[backside];
		writer.fn(&writer, buffer.data());
	}
}

static
auto write_events(const scuff::device& dev, const scuff_event_writer& writer, size_t backside) -> void {
	auto& ab               = dev.external->shm_device.data->events_in;
	auto& buffer           = ab.value[backside];
	const auto event_count = writer.count(&writer);
	for (size_t j = 0; j < event_count; j++) {
		const auto header = writer.get(&writer, j);
		buffer.push_back(convert(*header)); // TODO: remember to clear this in the sandbox process
	}
}

static
auto write_entry_ports(const scuff::model& m, const scuff_input_devices& devices, size_t backside) -> void {
	for (size_t i = 0; i < devices.count; i++) {
		const auto& item  = devices.devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		write_audio(dev, item.audio_writers, backside);
		write_events(dev, item.event_writer, backside);
	}
}

static
auto read_audio(const scuff::device& dev, const scuff_audio_readers& readers, size_t frontside) -> void {
	for (size_t j = 0; j < readers.count; j++) {
		const auto& reader = readers.readers[j];
		const auto& ab     = dev.external->shm_audio_ports.output_buffers[reader.port_index];
		const auto& buffer = ab.value[frontside];
		reader.fn(&reader, buffer.data());
	}
}

static
auto read_zeros(const scuff::device& dev, const scuff_audio_readers& readers) -> void {
	std::array<float, SCUFF_CHANNEL_COUNT * SCUFF_VECTOR_SIZE> zeros = {0.0f};
	for (size_t j = 0; j < readers.count; j++) {
		const auto& reader = readers.readers[j];
		reader.fn(&reader, zeros.data());
	}
}

static
auto read_events(const scuff::device& dev, const scuff_event_reader& reader, size_t frontside) -> void {
	auto& ab               = dev.external->shm_device.data->events_out;
	auto& buffer           = ab.value[frontside];
	const auto event_count = buffer.size();
	for (size_t j = 0; j < event_count; j++) {
		const auto& event  = buffer[j];
		const auto& header = convert(event);
		reader.push(&reader, &header);
	}
	buffer.clear();
}

static
auto read_exit_ports(const scuff::model& m, const scuff::group& group, const scuff_output_devices& devices, size_t frontside) -> void {
	if (scuff::wait_for_output_ready(group, frontside)) {
		for (size_t i = 0; i < devices.count; i++) {
			const auto& item  = devices.devices[i];
			const auto dev_id = id::device{item.dev};
			const auto& dev   = m.devices.at(dev_id);
			read_audio(dev, item.audio_readers, frontside);
			read_events(dev, item.event_reader, frontside);
		}
	}
	else {
		for (size_t i = 0; i < devices.count; i++) {
			const auto& item  = devices.devices[i];
			const auto dev_id = id::device{item.dev};
			const auto& dev   = m.devices.at(dev_id);
			read_zeros(dev, item.audio_readers);
		}
	}
}

static
auto signal_sandbox_processing(const scuff::group& group, uint64_t epoch, size_t backside) -> void {
	auto& data = *group.external->shm.data;
	// Set the sandbox counter
	data.sandboxes_processing.value[backside].store(group.sandboxes.size(), std::memory_order_seq_cst);
	// Set the epoch. This will trigger the sandboxes to begin processing.
	data.epoch.store(epoch, std::memory_order_release);
}

static
auto report_error(std::string_view err) -> void {
	DATA_->callbacks.on_error.fn(&DATA_->callbacks.on_error, err.data());
}

static
auto get_out_messages(const sandbox& sbox) -> std::vector<msg::out::msg> {
	std::vector<msg::out::msg> msgs;
	sbox.external->shm.data->msgs_out.take(&msgs);
	return msgs;
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::device_create_error& msg) -> void {
	// We sent a device_create message to the sandbox but it failed to create the remote device.
	const auto m         = DATA_->working_model.lock();
	const auto sbox      = m->sandboxes.at(sbox_id);
	const auto return_fn = sbox.external->return_buffers.devices.take(msg.callback);
	*m = set_error(std::move(*m), msg.dev, "Failed to create.");
	return_fn.fn(&return_fn, msg.dev.value);
	DATA_->callbacks.on_device_error.fn(&DATA_->callbacks.on_device_error, msg.dev.value);
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::device_create_success& msg) -> void {
	// We sent a device_create message to the sandbox and it succeeded in creating the remote device.
	const auto m                  = DATA_->working_model.lock();
	const auto device             = m->devices.at(msg.dev);
	const auto sbox               = m->sandboxes.at(sbox_id);
	const auto return_fn          = sbox.external->return_buffers.devices.take(msg.callback);
	const auto device_shmid       = scuff::DATA_->instance_id + "+dev+" + std::to_string(msg.dev.value);
	const auto device_ports_shmid = scuff::DATA_->instance_id + "+dev+" + std::to_string(msg.dev.value) + "+ports";
	// These shared memory segments were created by the sandbox process
	// but we're also going to remove them when we're done with them.
	device.external->shm_device      = shm::device{bip::open_only, shm::segment::remove_when_done, device_shmid};
	device.external->shm_audio_ports = shm::device_audio_ports{bip::open_only, shm::segment::remove_when_done, device_ports_shmid};
	return_fn.fn(&return_fn, msg.dev.value);
	publish(*m); // Device may not have been published yet.
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::device_params_changed& msg) -> void {
	DATA_->callbacks.on_device_params_changed.fn(&DATA_->callbacks.on_device_params_changed, msg.dev.value);
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_param& msg) -> void {
	const auto m         = DATA_->working_model.lock();
	const auto sbox      = m->sandboxes.at(sbox_id);
	const auto return_fn = sbox.external->return_buffers.params.take(msg.callback);
	return_fn.fn(&return_fn, msg.param.value);
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_param_value& msg) -> void {
	const auto m         = DATA_->working_model.lock();
	const auto sbox      = m->sandboxes.at(sbox_id);
	const auto return_fn = sbox.external->return_buffers.doubles.take(msg.callback);
	return_fn.fn(&return_fn, msg.value);
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_param_value_text& msg) -> void {
	const auto m         = DATA_->working_model.lock();
	const auto sbox      = m->sandboxes.at(sbox_id);
	const auto return_fn = sbox.external->return_buffers.strings.take(msg.callback);
	const auto text      = sbox.external->shm.data->strings.take(msg.STR_text);
	return_fn.fn(&return_fn, text.c_str());
}

static
auto process_message(id::sandbox sbox_id, const msg::out::msg& msg) -> void {
	const auto proc = [sbox_id](const auto& msg) -> void { process_message_(sbox_id, msg); };
	try                               { fast_visit(proc, msg); }
	catch (const std::exception& err) { report_error(err.what()); }
}

static
auto send_messages(const sandbox& sbox) -> void {
	const auto queue      = sbox.external->msg_queue.lock();
	const auto push_count = sbox.external->shm.data->msgs_in.push_as_many_as_possible(*queue);
	for (size_t i = 0; i < push_count; i++) {
		queue->pop_front();
	}
}

static
auto receive_messages(const sandbox& sbox) -> void {
	const auto msgs = get_out_messages(sbox);
	for (const auto& msg : msgs) {
		process_message(sbox.id, msg);
	}
}

static
auto process_sandbox_messages(const sandbox& sbox) -> void {
	send_messages(sbox);
	receive_messages(sbox);
}

static
auto process_sandbox_messages() -> void {
	const auto sandboxes = DATA_->working_model.lock()->sandboxes;
	for (const auto& sbox : sandboxes) {
		process_sandbox_messages(sbox);
	}
}

static
auto poll_thread(std::stop_token stop_token) -> void {
	auto now     = std::chrono::steady_clock::now();
	auto next_gc = now + std::chrono::milliseconds{SCUFF_GC_INTERVAL_MS};
	while (!stop_token.stop_requested()) {
		now = std::chrono::steady_clock::now();
		if (now > next_gc) {
			DATA_->published_model.garbage_collect();
			next_gc = now + std::chrono::milliseconds{SCUFF_GC_INTERVAL_MS};
		}
		process_sandbox_messages();
		std::this_thread::sleep_for(std::chrono::milliseconds{SCUFF_POLL_SLEEP_MS});
	}
}

static
auto is_running(const sandbox& sbox) -> bool {
	return sbox.external->proc && sbox.external->proc->running();
}

static
auto is_running(scuff_sbox sbox) -> bool {
	return is_running(scuff::DATA_->working_model.lock()->sandboxes.at({sbox}));
}

static
auto close_all_editors() -> void {
	const auto sandboxes = scuff::DATA_->working_model.lock()->sandboxes;
	for (const auto& sandbox : sandboxes) {
		if (is_running(sandbox)) {
			send(sandbox, scuff::msg::in::close_all_editors{});
		}
	}
}

static
auto device_connect(scuff_device dev_out, size_t port_out, scuff_device dev_in, size_t port_in) -> void {
	// TODO:
}

static
auto plugin_find(scuff_plugin_id plugin_id) -> scuff_plugin {
	const auto& m = scuff::DATA_->working_model.lock();
	for (const auto& plugin : m->plugins) {
		if (plugin.ext_id.value == plugin_id) {
			return plugin.id.value;
		}
	}
	return scuff::id::plugin{}.value;
}

static
auto device_create(scuff_sbox sbox_id, scuff_plugin_type type, scuff_plugin_id plugin_id, scuff_return_device fn) -> void {
	const auto m      = scuff::DATA_->working_model.lock();
	const auto& sbox  = m->sandboxes.at({sbox_id});
	scuff::device dev;
	dev.id            = scuff::id::device{scuff::id_gen_++};
	dev.sbox          = {sbox_id};
	dev.plugin_ext_id = {plugin_id};
	dev.plugin        = id::plugin{plugin_find(plugin_id)};
	if (!dev.plugin) {
		set_error(dev.id, "Plugin not found.");
		scuff::DATA_->callbacks.on_device_error.fn(&scuff::DATA_->callbacks.on_device_error, dev.id.value);
		scuff::insert_device(dev);
		return;
	}
	const auto str_plugin_id = sbox.external->shm.data->strings.put(plugin_id);
	const auto callback      = sbox.external->return_buffers.devices.put(fn);
	send(sbox, msg::in::device_create{dev.id, type, str_plugin_id, callback});
	scuff::insert_device(dev);
}

auto device_disconnect(scuff_device dev_out, size_t port_out, scuff_device dev_in, size_t port_in) -> void {
	// TODO:
}

auto device_duplicate(scuff_device dev, scuff_sbox sbox, scuff_return_device fn) -> void {
	// TODO: similar to create()?
}

static
auto device_get_error(scuff_device device) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->devices.at({device}).error->c_str();
}

static
auto device_get_name(scuff_device dev) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->devices.at({dev}).name->c_str();
}

static
auto device_get_param_count(scuff_device dev) -> size_t {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& shm    = device.external->shm_device;
	if (!shm.data) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return 0;
	}
	const auto lock = std::unique_lock{shm.data->mutex};
	return device.external->shm_device.data->param_count;
}

static
auto device_get_param_value_text(scuff_device dev, scuff_param param, double value, scuff_return_string fn) -> void {
	const auto& m       = scuff::DATA_->working_model.lock();
	const auto& device  = m->devices.at({dev});
	const auto& sbox    = m->sandboxes.at(device.sbox);
	const auto callback = sbox.external->return_buffers.strings.put(fn);
	send(sbox, scuff::msg::in::get_param_value_text{dev, param, value, callback});
}

static
auto device_get_plugin(scuff_device dev) -> scuff_plugin {
	const auto m = scuff::DATA_->working_model.lock();
	return m->devices.at({dev}).plugin.value;
}

static
auto device_has_gui(scuff_device dev) -> bool {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto lock    = std::unique_lock{device.external->shm_device.data->mutex};
	const auto flags   = device.external->shm_device.data->flags;
	return flags.value & flags.has_gui;
}

static
auto device_has_params(scuff_device dev) -> bool {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto lock    = std::unique_lock{device.external->shm_device.data->mutex};
	const auto flags   = device.external->shm_device.data->flags;
	return flags.value & flags.has_params;
}

static
auto device_set_render_mode(scuff_device dev, scuff_render_mode mode) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::device_set_render_mode{dev, mode});
}

static
auto device_gui_hide(scuff_device dev) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::device_gui_hide{dev});
}

static
auto device_gui_show(scuff_device dev) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::device_gui_show{dev});
}

static
auto device_was_loaded_successfully(scuff_device dev) -> bool {
	const auto m = scuff::DATA_->working_model.lock();
	return m->devices.at({dev}).error->empty();
}

static
auto group_create() -> scuff_group {
	const auto m = scuff::DATA_->working_model.lock();
	scuff::group group;
	group.id     = scuff::id::group{scuff::id_gen_++};
	try {
		const auto shmid = scuff::DATA_->instance_id + "+group+" + std::to_string(group.id.value);
		group.external = std::make_shared<scuff::group_external>();
		group.external->shm = shm::group{bip::create_only, shm::segment::remove_when_done, shmid};
	} catch (const std::exception& err) {
		scuff::DATA_->callbacks.on_error.fn(&scuff::DATA_->callbacks.on_error, err.what());
		return -1;
	}
	scuff::insert_group(group);
	return {group.id.value};
}

static
auto is_scanning() -> bool {
	return scuff::scan::is_running();
}

static
auto param_get_value(scuff_device dev, scuff_param param, scuff_return_double fn) -> void {
	const auto& m       = scuff::DATA_->working_model.lock();
	const auto& device  = m->devices.at({dev});
	const auto& sbox    = m->sandboxes.at(device.sbox);
	const auto callback = sbox.external->return_buffers.doubles.put(fn);
	send(sbox, scuff::msg::in::get_param_value{dev, param, callback});
}

static
auto param_find(scuff_device dev, scuff_param_id param_id, scuff_return_param fn) -> void {
	const auto& model       = scuff::DATA_->working_model.lock();
	const auto& device      = model->devices.at({dev});
	const auto& sbox        = model->sandboxes.at(device.sbox);
	const auto callback     = sbox.external->return_buffers.params.put(fn);
	const auto STR_param_id = sbox.external->shm.data->strings.put(param_id);
	send(sbox, scuff::msg::in::find_param{dev, STR_param_id, callback});
}

static
auto param_gesture_begin(scuff_device dev, scuff_param param) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::event{dev, scuff_event_param_gesture_begin{/*TODO:*/}});
}

static
auto param_gesture_end(scuff_device dev, scuff_param param) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::event{dev, scuff_event_param_gesture_end{/*TODO:*/}});
}

static
auto param_set_value(scuff_device dev, scuff_param param, double value) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::event{dev, scuff_event_param_value{/*TODO:*/}});
}

static
auto plugfile_get_path(scuff_plugfile plugfile) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugfiles.at({plugfile}).path->c_str();
}

static
auto plugfile_get_error(scuff_plugfile plugfile) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugfiles.at({plugfile}).error->c_str();
}

static
auto plugin_get_error(scuff_plugin plugin) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).error->c_str();
}

static
auto plugin_get_id(scuff_plugin plugin) -> scuff_plugin_id {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).ext_id.value.c_str();
}

static
auto plugin_get_name(scuff_plugin plugin) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).name->c_str();
}

static
auto plugin_get_vendor(scuff_plugin plugin) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).vendor->c_str();
}

static
auto plugin_get_version(scuff_plugin plugin) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).version->c_str();
}

static
auto restart(scuff_sbox sbox, const char* sbox_exe_path) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto sandbox = m->sandboxes.at({sbox});
	const auto& group  = m->groups.at(sandbox.group);
	if (sandbox.external->proc && sandbox.external->proc->running()) {
		sandbox.external->proc->terminate();
	}
	const auto group_shmid   = group.external->shm.id();
	const auto sandbox_shmid = sandbox.external->shm.id();
	const auto exe_args      = scuff::make_sbox_exe_args(group_shmid, sandbox_shmid);
	sandbox.external->proc   = std::make_unique<bp::child>(sbox_exe_path, exe_args);
}

static
auto do_scan(const char* scan_exe_path, int flags) -> void {
	// TODO: handle the flags
	scuff::scan::stop_if_it_is_already_running();
	scuff::scan::start(scan_exe_path);
}

auto sandbox_create(scuff_group group_id, const char* sbox_exe_path) -> scuff_sbox {
	const auto m = scuff::DATA_->working_model.lock();
	scuff::sandbox sbox;
	sbox.id = scuff::id::sandbox{scuff::id_gen_++};
	try {
		const auto& group          = m->groups.at({group_id});
		const auto group_shmid     = group.external->shm.id();
		const auto sandbox_shmid   = scuff::DATA_->instance_id + "+sbox+" + std::to_string(sbox.id.value);
		const auto exe_args        = scuff::make_sbox_exe_args(group_shmid, sandbox_shmid);
		sbox.group                 = {group_id};
		sbox.external              = std::make_shared<scuff::sandbox_external>();
		sbox.external->shm         = shm::sandbox{bip::create_only, shm::segment::remove_when_done, sandbox_shmid};
		sbox.external->proc        = std::make_unique<bp::child>(sbox_exe_path, exe_args);
		// Add sandbox to group
		*m = add_sandbox_to_group(std::move(*m), {group_id}, sbox.id);
	}
	catch (const std::exception& err) {
		sbox.error = err.what();
		scuff::DATA_->callbacks.on_sbox_error.fn(&scuff::DATA_->callbacks.on_sbox_error, sbox.id.value);
	}
	scuff::insert_sandbox(sbox);
	scuff::publish(*m);
	return sbox.id.value;
}

static
auto sandbox_erase(scuff_sbox sbox) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	const auto& sandbox = m->sandboxes.at({sbox});
	*m = remove_sandbox_from_group(std::move(*m), sandbox.group, {sbox});
	*m = erase_sandbox(std::move(*m), {sbox});
	scuff::publish(*m);
}

static
auto sandbox_get_error(scuff_sbox sbox) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->sandboxes.at({sbox}).error->c_str();
}

static
auto set_sample_rate(scuff_sample_rate sr) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	for (const auto& sbox : m->sandboxes) {
		if (sbox.external->proc && sbox.external->proc->running()) {
			send(sbox, scuff::msg::in::set_sample_rate{sr});
		}
	}
}

} // scuff

auto scuff_audio_process(scuff_group_process process) -> void {
	const auto audio     = scuff::DATA_->published_model.read();
	const auto& group    = audio->groups.at({process.group});
	const auto epoch     = ++group.external->epoch;
	const auto backside  = (epoch + 0) % 2;
	const auto frontside = (epoch + 1) % 2;
	scuff::write_entry_ports(*audio, process.input_devices, backside);
	scuff::signal_sandbox_processing(group, epoch, backside);
	scuff::read_exit_ports(*audio, group, process.output_devices, frontside);
}

auto scuff_init(const scuff_config* config) -> bool {
	if (scuff::initialized_) { return true; }
	scuff::DATA_              = std::make_unique<scuff::data>();
	scuff::DATA_->callbacks   = config->callbacks;
	scuff::DATA_->instance_id = "scuff+" + std::to_string(scuff::os::get_process_id());
	try {
		scuff::DATA_->poll_thread = std::jthread{scuff::poll_thread};
		scuff::initialized_       = true;
		return true;
	} catch (const std::exception& err) {
		scuff::DATA_.reset();
		config->callbacks.on_error.fn(&config->callbacks.on_error, err.what());
		return false;
	}
}

auto scuff_shutdown() -> void {
	if (!scuff::initialized_) { return; }
	scuff::DATA_->poll_thread.request_stop();
	scuff::DATA_->poll_thread.join();
	scuff::DATA_.reset();
	scuff::initialized_ = false;
}

auto scuff_close_all_editors() -> void {
	try                               { scuff::close_all_editors(); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_connect(scuff_device dev_out, size_t port_out, scuff_device dev_in, size_t port_in) -> void {
	try                               { scuff::device_connect(dev_out, port_out, dev_in, port_in); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_create(scuff_sbox sbox, scuff_plugin_type type, scuff_plugin_id plugin_id, scuff_return_device fn) -> void {
	try                               { scuff::device_create(sbox, type, plugin_id, fn); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_disconnect(scuff_device dev_out, size_t port_out, scuff_device dev_in, size_t port_in) -> void {
	try                               { scuff::device_disconnect(dev_out, port_out, dev_in, port_in); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_duplicate(scuff_device dev, scuff_sbox sbox, scuff_return_device fn) -> void {
	try                               { scuff::device_duplicate(dev, sbox, fn); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_get_error(scuff_device device) -> const char* {
	try                               { return scuff::device_get_error(device); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_device_get_name(scuff_device dev) -> const char* {
	try                               { return scuff::device_get_name(dev); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_device_get_param_count(scuff_device dev) -> size_t {
	try                               { return scuff::device_get_param_count(dev); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return 0; }
}

auto scuff_device_get_param_value_text(scuff_device dev, scuff_param param, double value, scuff_return_string fn) -> void {
	try                               { scuff::device_get_param_value_text(dev, param, value, fn); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_get_plugin(scuff_device dev) -> scuff_plugin {
	try                               { return scuff::device_get_plugin(dev); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return {}; }
}

auto scuff_device_has_gui(scuff_device dev) -> bool {
	try                               { return scuff::device_has_gui(dev); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return false; }
}

auto scuff_device_has_params(scuff_device dev) -> bool {
	try                               { return scuff::device_has_params(dev); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return false; }
}

auto scuff_device_set_render_mode(scuff_device dev, scuff_render_mode mode) -> void {
	try                               { scuff::device_set_render_mode(dev, mode); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_gui_hide(scuff_device dev) -> void {
	try                               { scuff::device_gui_hide(dev); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_gui_show(scuff_device dev) -> void {
	try                               { scuff::device_gui_show(dev); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_device_was_loaded_successfully(scuff_device dev) -> bool {
	try                               { return scuff::device_was_loaded_successfully(dev); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return false; }
}

auto scuff_group_create() -> scuff_group {
	try                               { return scuff::group_create(); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return {}; }
}

auto scuff_is_running(scuff_sbox sbox) -> bool {
	try                               { return scuff::is_running(sbox); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return false; }
}

auto scuff_is_scanning() -> bool {
	try                               { return scuff::is_scanning(); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return false; }
}

auto scuff_param_get_value(scuff_device dev, scuff_param param, scuff_return_double fn) -> void {
	try                               { scuff::param_get_value(dev, param, fn); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_param_find(scuff_device dev, scuff_param_id param_id, scuff_return_param fn) -> void {
	try                               { scuff::param_find(dev, param_id, fn); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_param_gesture_begin(scuff_device dev, scuff_param param) -> void {
	try                               { scuff::param_gesture_begin(dev, param); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_param_gesture_end(scuff_device dev, scuff_param param) -> void {
	try                               { scuff::param_gesture_end(dev, param); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_param_set_value(scuff_device dev, scuff_param param, double value) -> void {
	try                               { scuff::param_set_value(dev, param, value); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_plugin_find(scuff_plugin_id plugin_id) -> scuff_plugin {
	try                               { return scuff::plugin_find(plugin_id); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return {}; }
}

auto scuff_plugfile_get_path(scuff_plugfile plugfile) -> const char* {
	try                               { return scuff::plugfile_get_path(plugfile); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_plugfile_get_error(scuff_plugfile plugfile) -> const char* {
	try                               { return scuff::plugfile_get_error(plugfile); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_plugin_get_error(scuff_plugin plugin) -> const char* {
	try                               { return scuff::plugin_get_error(plugin); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_plugin_get_id(scuff_plugin plugin) -> scuff_plugin_id {
	try                               { return scuff::plugin_get_id(plugin); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return {}; }
}

auto scuff_plugin_get_name(scuff_plugin plugin) -> const char* {
	try                               { return scuff::plugin_get_name(plugin); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_plugin_get_vendor(scuff_plugin plugin) -> const char* {
	try                               { return scuff::plugin_get_vendor(plugin); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_plugin_get_version(scuff_plugin plugin) -> const char* {
	try                               { return scuff::plugin_get_version(plugin); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_restart(scuff_sbox sbox, const char* sbox_exe_path) -> void {
	try                               { scuff::restart(sbox, sbox_exe_path); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_scan(const char* scan_exe_path, int flags) -> void {
	try                               { scuff::do_scan(scan_exe_path, flags); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_sandbox_create(scuff_group group_id, const char* sbox_exe_path) -> scuff_sbox {
	try                               { return scuff::sandbox_create(group_id, sbox_exe_path); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return {}; }
}

auto scuff_sandbox_erase(scuff_sbox sbox) -> void {
	try                               { scuff::sandbox_erase(sbox); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}

auto scuff_sandbox_get_error(scuff_sbox sbox) -> const char* {
	try                               { return scuff::sandbox_get_error(sbox); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_set_sample_rate(scuff_sample_rate sr) -> void {
	try                               { scuff::set_sample_rate(sr); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
}
