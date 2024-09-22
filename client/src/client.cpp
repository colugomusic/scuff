#include "common/speen.hpp"
#include "common/types.hpp"
#include "common/visit.hpp"
#include "scan.hpp"
#include <mutex>
#include <readerwriterqueue.h>
#include <string>
#include <variant>

namespace bip = boost::interprocess;

namespace scuff {

[[nodiscard]] static
auto make_sbox_exe_args(std::string_view group_id, std::string_view sandbox_id) -> std::vector<std::string> {
	std::vector<std::string> args;
	args.push_back(std::string("--group ") + group_id.data());
	args.push_back(std::string("--sandbox ") + sandbox_id.data());
	return args;
}

static
auto write_audio(const scuff::device& dev, const scuff_audio_writers& writers) -> void {
	for (size_t j = 0; j < writers.count; j++) {
		const auto& writer = writers.writers[j];
		auto& buffer       = dev.shm->data->audio_in[writer.port_index];
		writer.fn(&writer, buffer.data());
	}
}

static
auto write_events(const scuff::device& dev, const scuff_event_writer& writer) -> void {
	auto& buffer           = dev.shm->data->events_in;
	const auto event_count = std::min(writer.count(&writer), size_t(SCUFF_EVENT_PORT_SIZE));
	for (size_t j = 0; j < event_count; j++) {
		const auto header = writer.get(&writer, j);
		if (const auto converted = convert(*header)) {
			buffer.push_back(*converted); // TODO: remember to clear this in the sandbox process
		}
	}
}

static
auto write_entry_ports(const scuff::model& m, const scuff_input_devices& devices) -> void {
	for (size_t i = 0; i < devices.count; i++) {
		const auto& item  = devices.devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		write_audio(dev, item.audio_writers);
		write_events(dev, item.event_writer);
	}
}

static
auto read_audio(const scuff::device& dev, const scuff_audio_readers& readers) -> void {
	for (size_t j = 0; j < readers.count; j++) {
		const auto& reader = readers.readers[j];
		const auto& buffer = dev.shm->data->audio_out[reader.port_index];
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
auto read_events(const scuff::device& dev, const scuff_event_reader& reader) -> void {
	auto& buffer           = dev.shm->data->events_out;
	const auto event_count = buffer.size();
	for (size_t j = 0; j < event_count; j++) {
		const auto& event  = buffer[j];
		reader.push(&reader, &scuff::convert(event));
	}
	buffer.clear();
}

static
auto read_exit_ports(const scuff::model& m, const scuff_output_devices& devices) -> void {
	for (size_t i = 0; i < devices.count; i++) {
		const auto& item  = devices.devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		read_audio(dev, item.audio_readers);
		read_events(dev, item.event_reader);
	}
}

static
auto read_zeros(const scuff::model& m, const scuff_output_devices& devices) -> void {
	for (size_t i = 0; i < devices.count; i++) {
		const auto& item  = devices.devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		read_zeros(dev, item.audio_readers);
	}
}

static
auto signal_sandbox_processing(const scuff::group& group, uint64_t epoch) -> void {
	auto& data = *group.service->shm.data;
	// Set the sandbox counter
	data.sandboxes_processing.store(group.sandboxes.size());
	auto lock = std::unique_lock{data.mut};
	// Set the epoch.
	data.epoch.store(epoch, std::memory_order_release);
	// Signal sandboxes to start processing.
	data.cv.notify_all();
}

[[nodiscard]] static
auto wait_for_all_sandboxes_done(const scuff::group& group) -> bool {
	 // the sandboxes not completing their work within 1 second is a
	 // catastrophic failure.
	static constexpr auto MAX_WAIT_TIME = std::chrono::seconds{1};
	auto& data = *group.service->shm.data;
	auto done = [&data]() -> bool {
		return data.sandboxes_processing.load(std::memory_order_acquire) < 1;
	};
	if (done()) {
		return true;
	}
	auto lock = std::unique_lock{data.mut};
	return group.service->shm.data->cv.wait_for(lock, MAX_WAIT_TIME, done);
}

[[nodiscard]] static
auto do_sandbox_processing(const scuff::group& group, uint64_t epoch) -> bool {
	signal_sandbox_processing(group, epoch);
	return wait_for_all_sandboxes_done(group);
}

static
auto report_error(std::string_view err) -> void {
	DATA_->callbacks.on_error.fn(&DATA_->callbacks.on_error, err.data());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_created_device& msg) -> void {
	const auto m         = DATA_->model.lock_write();
	const auto sbox      = m->sandboxes.at(sbox_id);
	const auto return_fn = sbox.service->return_buffers.devices.take(msg.callback);
	if (!msg.ports_shmid.empty()) {
		// The sandbox succeeded in creating the remote device.
		auto device             = m->devices.at({msg.dev_id});
		const auto device_shmid = shm::device::make_id(DATA_->instance_id, {msg.dev_id});
		device.shm = shm::device{bip::open_only, shm::segment::remove_when_done, device_shmid};
		m->devices = m->devices.insert(device);
		return_fn({msg.dev_id}, true);
		DATA_->model.lock_publish(*m); // Device may not have been published yet.
	}
	else {
		// The sandbox failed to create the remote device.
		const auto err = "Failed to create remote device.";
		*m = set_error(std::move(*m), {msg.dev_id}, err);
		return_fn({msg.dev_id}, false);
		DATA_->callbacks.on_device_error.fn(&DATA_->callbacks.on_device_error, msg.dev_id, err);
	}
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::device_param_info_changed& msg) -> void {
	DATA_->callbacks.on_device_params_changed.fn(&DATA_->callbacks.on_device_params_changed, msg.dev_id);
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::report_error& msg) -> void {
	DATA_->callbacks.on_sbox_error.fn(&DATA_->callbacks.on_sbox_error, sbox_id.value, msg.text.c_str());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::report_fatal_error& msg) -> void {
	// This message could be received if the sandbox process
	// manages to prematurely terminate itself in a "clean" way.
	DATA_->callbacks.on_sbox_crashed.fn(&DATA_->callbacks.on_sbox_crashed, sbox_id.value, msg.text.c_str());
	// TODO: terminate the sandbox process if it is still running and figure out what else needs to be done here.
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::report_info& msg) -> void {
	DATA_->callbacks.on_sbox_info.fn(&DATA_->callbacks.on_sbox_info, sbox_id.value, msg.text.c_str());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::report_warning& msg) -> void {
	DATA_->callbacks.on_sbox_warning.fn(&DATA_->callbacks.on_sbox_warning, sbox_id.value, msg.text.c_str());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_param_value& msg) -> void {
	const auto m         = DATA_->model.lock_read();
	const auto sbox      = m.sandboxes.at(sbox_id);
	const auto return_fn = sbox.service->return_buffers.doubles.take(msg.callback);
	return_fn.fn(&return_fn, msg.value);
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_param_value_text& msg) -> void {
	const auto m         = DATA_->model.lock_read();
	const auto sbox      = m.sandboxes.at(sbox_id);
	const auto return_fn = sbox.service->return_buffers.strings.take(msg.callback);
	return_fn.fn(&return_fn, msg.text.c_str());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_state& msg) -> void {
	const auto m    = DATA_->model.lock_read();
	const auto sbox = m.sandboxes.at(sbox_id);
	const auto return_fn = sbox.service->return_buffers.states.take(msg.callback);
	return_fn(msg.bytes);
}

static
auto process_message(id::sandbox sbox_id, const msg::out::msg& msg) -> void {
	 const auto proc = [sbox_id](const auto& msg) -> void { process_message_(sbox_id, msg); };
	 try                               { fast_visit(proc, msg); }
	 catch (const std::exception& err) { report_error(err.what()); }
}

static
auto process_sandbox_messages(const sandbox& sbox) -> void {
	if (!sbox.service->proc.running()) {
		// The sandbox process has stopped unexpectedly.
		// TODO: Handle this situation
		return;
	}
	sbox.service->send_msgs();
	const auto msgs = sbox.service->receive_msgs();
	for (const auto& msg : msgs) {
		process_message(sbox.id, msg);
	}
}

static
auto process_sandbox_messages() -> void {
	const auto sandboxes = DATA_->model.lock_read().sandboxes;
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
			DATA_->model.lock_gc();
			next_gc = now + std::chrono::milliseconds{SCUFF_GC_INTERVAL_MS};
		}
		process_sandbox_messages();
		std::this_thread::sleep_for(std::chrono::milliseconds{SCUFF_POLL_SLEEP_MS});
	}
}

static
auto is_running(const sandbox& sbox) -> bool {
	return sbox.service->proc.running();
}

static
auto is_running(scuff_sbox sbox) -> bool {
	return is_running(scuff::DATA_->model.lock_read().sandboxes.at({sbox}));
}

static
auto close_all_editors() -> void {
	const auto sandboxes = scuff::DATA_->model.lock_read().sandboxes;
	for (const auto& sandbox : sandboxes) {
		if (is_running(sandbox)) {
			sandbox.service->enqueue(scuff::msg::in::close_all_editors{});
		}
	}
}

static
auto device_connect(scuff_device dev_out_id, size_t port_out, scuff_device dev_in_id, size_t port_in) -> void {
	const auto m        = DATA_->model.lock_write();
	const auto& dev_out = m->devices.at({dev_out_id});
	const auto& dev_in  = m->devices.at({dev_in_id});
	if (dev_out.sbox == dev_in.sbox) {
		// Devices are in the same sandbox
		const auto& sbox = m->sandboxes.at(dev_out.sbox);
		sbox.service->enqueue(scuff::msg::in::device_connect{dev_out_id, port_out, dev_in_id, port_in});
		return;
	}
	// Devices are in different sandboxes
	const auto& sbox_out    = m->sandboxes.at(dev_out.sbox);
	const auto& sbox_in     = m->sandboxes.at(dev_in.sbox);
	const auto group_out_id = sbox_out.group;
	const auto group_in_id  = sbox_in.group;
	if (group_out_id != group_in_id) {
		throw std::runtime_error("Cannot connect devices that exist in different sandbox groups.");
	}
	auto group = m->groups.at(group_out_id);
	group.cross_sbox_conns = group.cross_sbox_conns.set(id::device{dev_out_id}, id::device{dev_in_id});
	m->groups = m->groups.insert(group);
	DATA_->model.lock_publish(*m);
}

static
auto plugin_find(const model& m, scuff_plugin_id plugin_id) -> scuff_plugin {
	for (const auto& plugin : m.plugins) {
		if (plugin.ext_id.value == plugin_id) {
			return plugin.id.value;
		}
	}
	return scuff::id::plugin{}.value;
}

static
auto plugin_find(scuff_plugin_id plugin_id) -> scuff_plugin {
	const auto m = DATA_->model.lock_read();
	return plugin_find(m, plugin_id);
}

[[nodiscard]] static
auto device_create(model&& m, const sandbox& sbox, scuff_plugin_type type, ext::id::plugin plugin_ext_id, id::plugin plugin_id, return_device return_fn) -> model {
	scuff::device dev;
	dev.id            = scuff::id::device{scuff::id_gen_++};
	dev.sbox          = {sbox.id};
	dev.plugin_ext_id = plugin_ext_id;
	dev.plugin        = plugin_id;
	dev.type          = type;
	m = scuff::add_device_to_sandbox(std::move(m), {sbox.id}, dev.id);
	m.devices = m.devices.insert(dev);
	if (!dev.plugin) {
		// We don't have this plugin yet so put the device into an error
		// state and call the return function immediately.
		const auto err = "Plugin not found.";
		m = set_error(std::move(m), dev.id, err);
		scuff::DATA_->callbacks.on_device_error.fn(&scuff::DATA_->callbacks.on_device_error, dev.id.value, err);
		return_fn(dev.id, false);
		return m;
	}
	// Plugin is available so we need to send a message to the sandbox to create the remote device.
	const auto callback = sbox.service->return_buffers.devices.put(return_fn);
	const auto plugin   = m.plugins.at(dev.plugin);
	const auto plugfile = m.plugfiles.at(plugin.plugfile);
	sbox.service->enqueue(msg::in::device_create{dev.id.value, type, plugfile.path, plugin_ext_id.value, callback});
	return m;
}

static
auto device_create(scuff_sbox sbox_id, scuff_plugin_type type, scuff_plugin_id plugin_ext_id, scuff_return_device fn) -> void {
	const auto m         = scuff::DATA_->model.lock_write();
	const auto& sbox     = m->sandboxes.at({sbox_id});
	const auto plugin_id = id::plugin{plugin_find(*m, plugin_ext_id)};
	const auto return_fn = [fn](id::device dev_id, bool success) { fn.fn(&fn, dev_id.value, success); };
	*m = device_create(std::move(*m), sbox, type, {plugin_ext_id}, plugin_id, return_fn);
}

static
auto device_disconnect(scuff_device dev_out_id, size_t port_out, scuff_device dev_in_id, size_t port_in) -> void {
	const auto m        = DATA_->model.lock_write();
	const auto& dev_out = m->devices.at({dev_out_id});
	const auto& dev_in  = m->devices.at({dev_in_id});
	if (dev_out.sbox == dev_in.sbox) {
		// Devices are in the same sandbox.
		const auto& sbox = m->sandboxes.at(dev_out.sbox);
		sbox.service->enqueue(scuff::msg::in::device_disconnect{dev_out_id, port_out, dev_in_id, port_in});
		return;
	}
	// Devices are in different sandboxes.
	const auto& sbox_out    = m->sandboxes.at(dev_out.sbox);
	const auto& sbox_in     = m->sandboxes.at(dev_in.sbox);
	const auto group_out_id = sbox_out.group;
	const auto group_in_id  = sbox_in.group;
	if (group_out_id != group_in_id) {
		throw std::runtime_error("Connected devices somehow exist in different sandbox groups?!");
	}
	auto group             = m->groups.at(group_out_id);
	group.cross_sbox_conns = group.cross_sbox_conns.erase(id::device{dev_out_id});
	m->groups              = m->groups.insert(group);
	DATA_->model.lock_publish(*m);
}

static
auto device_duplicate(scuff_device src_dev_id, scuff_sbox dst_sbox_id, scuff_return_device fn) -> void {
	const auto m             = scuff::DATA_->model.lock_write();
	const auto src_dev       = m->devices.at({src_dev_id});
	const auto src_sbox      = m->sandboxes.at(src_dev.sbox);
	const auto dst_sbox      = m->sandboxes.at({dst_sbox_id});
	const auto plugin_ext_id = src_dev.plugin_ext_id;
	const auto plugin        = src_dev.plugin;
	const auto type          = src_dev.type;
	// We're going to send a message to the source sandbox to save the source device.
	// When the saved state is returned, call this function with it:
	const auto save_cb       = src_sbox.service->return_buffers.states.put([plugin_ext_id, plugin, type, dst_sbox, fn](const std::vector<std::byte>& src_state) {
		// Now we're going to send a message to the destination sandbox to actually create the new device,
		// if the plugin is available.
		// When the new device is created, call this function with it:
		const auto return_fn = [dst_sbox, fn, src_state](id::device dev_id, bool success) {
			if (success) {
				// Remote device was created successfully.
				// Now send a message to the destination sandbox to load the saved state into the new device.
				dst_sbox.service->enqueue(scuff::msg::in::device_load{dev_id.value, src_state});
			}
			// Call user's callback
			fn.fn(&fn, dev_id.value, success);
		};
		const auto m = scuff::DATA_->model.lock_write();
		*m = device_create(std::move(*m), dst_sbox, type, plugin_ext_id, plugin, return_fn);
	});
	src_sbox.service->enqueue(msg::in::device_save{src_dev_id, save_cb});
}

static
auto device_erase(id::device dev_id) -> void {
	auto m            = DATA_->model.lock_write();
	const auto& dev   = m->devices.at(dev_id);
	const auto& sbox  = m->sandboxes.at(dev.sbox);
	const auto& group = m->groups.at(sbox.group);
	for (const auto sbox_id : group.sandboxes) {
		const auto& sbox = m->sandboxes.at(sbox_id);
		sbox.service->enqueue(msg::in::device_erase{dev_id.value});
	}
	*m = remove_device_from_sandbox(std::move(*m), dev.sbox, dev_id);
	m->devices = m->devices.erase(dev_id);
	DATA_->model.lock_publish(*m);
}

static
auto device_get_error(scuff_device device) -> const char* {
	return DATA_->model.lock_read().devices.at({device}).error->c_str();
}

static
auto device_get_name(scuff_device dev) -> const char* {
	return DATA_->model.lock_read().devices.at({dev}).name->c_str();
}

static
auto device_get_param_count(scuff_device dev) -> size_t {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at({dev});
	if (!device.shm->is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return 0;
	}
	const auto lock = std::unique_lock{device.shm->data->param_info_mutex};
	return device.shm->data->param_info.size();
}

static
auto device_get_param_value_text(scuff_device dev, scuff_param param, double value, scuff_return_string fn) -> void {
	const auto m        = DATA_->model.lock_read();
	const auto& device  = m.devices.at({dev});
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto callback = sbox.service->return_buffers.strings.put(fn);
	sbox.service->enqueue(msg::in::get_param_value_text{dev, param, value, callback});
}

static
auto device_get_plugin(scuff_device dev) -> scuff_plugin {
	return DATA_->model.lock_read().devices.at({dev}).plugin.value;
}

static
auto device_has_gui(scuff_device dev) -> bool {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at({dev});
	if (!device.shm->is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return false;
	}
	const auto flags   = device.shm->data->flags;
	return flags.value & flags.has_gui;
}

static
auto device_has_params(scuff_device dev) -> bool {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at({dev});
	if (!device.shm->is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return false;
	}
	const auto flags   = device.shm->data->flags;
	return flags.value & flags.has_params;
}

static
auto device_set_render_mode(scuff_device dev, scuff_render_mode mode) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at({dev});
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::device_set_render_mode{dev, mode});
}

static
auto device_gui_hide(scuff_device dev) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at({dev});
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::device_gui_hide{dev});
}

static
auto device_gui_show(scuff_device dev) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at({dev});
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::device_gui_show{dev});
}

static
auto device_was_loaded_successfully(scuff_device dev) -> bool {
	return DATA_->model.lock_read().devices.at({dev}).error->empty();
}

static
auto group_create() -> scuff_group {
	scuff::group group;
	const auto m = DATA_->model.lock_write();
	group.id     = id::group{id_gen_++};
	try {
		const auto shmid = shm::group::make_id(DATA_->instance_id, group.id);
		group.service = std::make_shared<group_service>();
		group.service->shm = shm::group{bip::create_only, shm::segment::remove_when_done, shmid};
	} catch (const std::exception& err) {
		DATA_->callbacks.on_error.fn(&DATA_->callbacks.on_error, err.what());
		return -1;
	}
	m->groups = m->groups.insert(group);
	return {group.id.value};
}

static
auto group_erase(id::group group_id) -> void {
	const auto m = DATA_->model.lock_write();
	const auto& group = m->groups.at(group_id);
	for (const auto sbox_id : group.sandboxes) {
		const auto& sbox = m->sandboxes.at(sbox_id);
		if (sbox.service->proc.running()) {
			sbox.service->proc.terminate();
		}
	}
	m->groups = m->groups.erase(group_id);
	DATA_->model.lock_publish(*m);
}

static
auto is_scanning() -> bool {
	return scan::is_running();
}

static
auto param_get_value(scuff_device dev, scuff_param param, scuff_return_double fn) -> void {
	const auto& m       = DATA_->model.lock_read();
	const auto& device  = m.devices.at({dev});
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto callback = sbox.service->return_buffers.doubles.put(fn);
	sbox.service->enqueue(scuff::msg::in::get_param_value{dev, param, callback});
}

static
auto param_find(id::device dev_id, scuff_param_id param_id) -> scuff_param {
	const auto m    = DATA_->model.lock_read();
	const auto dev  = m.devices.at(dev_id);
	const auto lock = std::unique_lock{dev.shm->data->param_info_mutex};
	for (size_t i = 0; i < dev.shm->data->param_info.size(); i++) {
		const auto& info = dev.shm->data->param_info[i];
		if (info.id == param_id) {
			return i;
		}
	}
	return SCUFF_INVALID_INDEX;
}

static
auto param_get_id(id::device dev_id, scuff_param param) -> scuff_param_id {
	const auto m    = DATA_->model.lock_read();
	const auto dev  = m.devices.at(dev_id);
	const auto lock = std::unique_lock{dev.shm->data->param_info_mutex};
	if (param >= dev.shm->data->param_info.size()) {
		throw std::runtime_error("Invalid parameter index.");
	}
	return dev.shm->data->param_info[param].id.c_str();
}

static
auto push_event(scuff_device dev, const scuff_event_header* event) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at({dev});
	const auto& sbox   = m.sandboxes.at(device.sbox);
	if (const auto converted = convert(*event)) {
		sbox.service->enqueue(scuff::msg::in::event{dev, *converted});
	}
}

static
auto plugfile_get_path(scuff_plugfile plugfile) -> const char* {
	return DATA_->model.lock_read().plugfiles.at({plugfile}).path->c_str();
}

static
auto plugfile_get_error(scuff_plugfile plugfile) -> const char* {
	return DATA_->model.lock_read().plugfiles.at({plugfile}).error->c_str();
}

static
auto plugin_get_error(scuff_plugin plugin) -> const char* {
	return DATA_->model.lock_read().plugins.at({plugin}).error->c_str();
}

static
auto plugin_get_id(scuff_plugin plugin) -> scuff_plugin_id {
	return DATA_->model.lock_read().plugins.at({plugin}).ext_id.value.c_str();
}

static
auto plugin_get_name(scuff_plugin plugin) -> const char* {
	return DATA_->model.lock_read().plugins.at({plugin}).name->c_str();
}

static
auto plugin_get_vendor(scuff_plugin plugin) -> const char* {
	return DATA_->model.lock_read().plugins.at({plugin}).vendor->c_str();
}

static
auto plugin_get_version(scuff_plugin plugin) -> const char* {
	return DATA_->model.lock_read().plugins.at({plugin}).version->c_str();
}

static
auto restart(scuff_sbox sbox, const char* sbox_exe_path) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto sandbox = m.sandboxes.at({sbox});
	const auto& group  = m.groups.at(sandbox.group);
	if (sandbox.service->proc.running()) {
		sandbox.service->proc.terminate();
	}
	const auto group_shmid   = group.service->shm.id();
	const auto sandbox_shmid = sandbox.service->get_shmid();
	const auto exe_args      = scuff::make_sbox_exe_args(group_shmid, sandbox_shmid);
	sandbox.service->proc   = bp::child{sbox_exe_path, exe_args};
	// TODO: the rest of this
}

static
auto do_scan(const char* scan_exe_path, int flags) -> void {
	scan::stop_if_it_is_already_running();
	scan::start(scan_exe_path, flags);
}

auto sandbox_create(scuff_group group_id, const char* sbox_exe_path) -> scuff_sbox {
	const auto m = DATA_->model.lock_write();
	sandbox sbox;
	sbox.id = id::sandbox{id_gen_++};
	try {
		const auto& group        = m->groups.at({group_id});
		const auto group_shmid   = group.service->shm.id();
		const auto sandbox_shmid = shm::sandbox::make_id(DATA_->instance_id, sbox.id);
		const auto exe_args      = make_sbox_exe_args(group_shmid, sandbox_shmid);
		auto proc                = bp::child{sbox_exe_path, exe_args};
		sbox.group               = {group_id};
		sbox.service            = std::make_shared<sandbox_service>(std::move(proc), sandbox_shmid);
		// Add sandbox to group
		*m = add_sandbox_to_group(std::move(*m), {group_id}, sbox.id);
	}
	catch (const std::exception& err) {
		sbox.error = err.what();
		DATA_->callbacks.on_sbox_error.fn(&DATA_->callbacks.on_sbox_error, sbox.id.value, err.what());
	}
	m->sandboxes = m->sandboxes.insert(sbox);
	DATA_->model.lock_publish(*m);
	return sbox.id.value;
}

static
auto sandbox_erase(scuff_sbox sbox) -> void {
	const auto m = DATA_->model.lock_write();
	const auto& sandbox = m->sandboxes.at({sbox});
	*m = remove_sandbox_from_group(std::move(*m), sandbox.group, {sbox});
	m->sandboxes = m->sandboxes.erase({sbox});
	DATA_->model.lock_publish(*m);
}

static
auto sandbox_get_error(scuff_sbox sbox) -> const char* {
	return DATA_->model.lock_read().sandboxes.at({sbox}).error->c_str();
}

static
auto set_sample_rate(scuff_sample_rate sr) -> void {
	const auto m = DATA_->model.lock_read();
	for (const auto& sbox : m.sandboxes) {
		if (sbox.service->proc.running()) {
			sbox.service->enqueue(msg::in::set_sample_rate{sr});
		}
	}
}

} // scuff

auto scuff_audio_process(scuff_group_process process) -> void {
	const auto audio     = scuff::DATA_->model.lockfree_read();
	const auto& group    = audio->groups.at({process.group});
	const auto epoch     = ++group.service->epoch;
	scuff::write_entry_ports(*audio, process.input_devices);
	if (scuff::do_sandbox_processing(group, epoch)) {
		scuff::read_exit_ports(*audio, process.output_devices);
	}
	else {
		scuff::read_zeros(*audio, process.output_devices);
	}
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

auto scuff_device_erase(scuff_device dev) -> void {
	try                               { scuff::device_erase({dev}); }
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

auto scuff_group_erase(scuff_group group) -> void {
	try                               { scuff::group_erase({group}); }
	catch (const std::exception& err) { scuff::report_error(err.what()); }
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

auto scuff_param_find(scuff_device dev, scuff_param_id param_id) -> scuff_param {
	try                               { return scuff::param_find({dev}, param_id); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return SCUFF_INVALID_INDEX; }
}

auto scuff_param_get_id(scuff_device dev, scuff_param param) -> scuff_param_id {
	try                               { return scuff::param_get_id({dev}, param); }
	catch (const std::exception& err) { scuff::report_error(err.what()); return ""; }
}

auto scuff_push_event(scuff_device dev, const scuff_event_header* event) -> void {
	try                               { scuff::push_event(dev, event); }
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
