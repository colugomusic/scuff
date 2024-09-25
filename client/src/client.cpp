#include "client.hpp"
#include "common/speen.hpp"
#include "common/types.hpp"
#include "common/visit.hpp"
#include "scan.hpp"
#include <mutex>
#include <readerwriterqueue.h>
#include <string>
#include <variant>

namespace bip = boost::interprocess;

namespace scuff::impl {

[[nodiscard]] static
auto make_sbox_exe_args(std::string_view group_id, std::string_view sandbox_id) -> std::vector<std::string> {
	std::vector<std::string> args;
	args.push_back(std::string("--group ") + group_id.data());
	args.push_back(std::string("--sandbox ") + sandbox_id.data());
	return args;
}

static
auto write_audio(const scuff::device& dev, const audio_writers& writers) -> void {
	for (size_t j = 0; j < writers.count; j++) {
		const auto& writer = writers.writers[j];
		auto& buffer       = dev.shm->data->audio_in[writer.port_index];
		writer.write(buffer.data());
	}
}

static
auto write_events(const scuff::device& dev, const event_writer& writer) -> void {
	auto& buffer           = dev.shm->data->events_in;
	const auto event_count = std::min(writer.count(), size_t(EVENT_PORT_SIZE));
	for (size_t j = 0; j < event_count; j++) {
		buffer.push_back(writer.get(j));
	}
}

static
auto write_entry_ports(const scuff::model& m, const input_devices& devices) -> void {
	for (size_t i = 0; i < devices.count; i++) {
		const auto& item  = devices.devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		write_audio(dev, item.audio_writers);
		write_events(dev, item.event_writer);
	}
}

static
auto read_audio(const scuff::device& dev, const audio_readers& readers) -> void {
	for (size_t j = 0; j < readers.count; j++) {
		const auto& reader = readers.readers[j];
		const auto& buffer = dev.shm->data->audio_out[reader.port_index];
		reader.read(buffer.data());
	}
}

static
auto read_zeros(const scuff::device& dev, const audio_readers& readers) -> void {
	std::array<float, CHANNEL_COUNT * VECTOR_SIZE> zeros = {0.0f};
	for (size_t j = 0; j < readers.count; j++) {
		const auto& reader = readers.readers[j];
		reader.read(zeros.data());
	}
}

static
auto read_events(const scuff::device& dev, const event_reader& reader) -> void {
	auto& buffer           = dev.shm->data->events_out;
	const auto event_count = buffer.size();
	for (size_t j = 0; j < event_count; j++) {
		reader.push(buffer[j]);
	}
	buffer.clear();
}

static
auto read_exit_ports(const scuff::model& m, const output_devices& devices) -> void {
	for (size_t i = 0; i < devices.count; i++) {
		const auto& item  = devices.devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		read_audio(dev, item.audio_readers);
		read_events(dev, item.event_reader);
	}
}

static
auto read_zeros(const scuff::model& m, const output_devices& devices) -> void {
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
	DATA_->callbacks.on_error(err.data());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_created_device& msg) -> void {
	auto m               = DATA_->model.lock_read();
	const auto sbox      = m.sandboxes.at(sbox_id);
	const auto return_fn = sbox.service->return_buffers.devices.take(msg.callback);
	if (!msg.ports_shmid.empty()) {
		// The sandbox succeeded in creating the remote device.
		auto device             = m.devices.at({msg.dev_id});
		const auto device_shmid = shm::device::make_id(DATA_->instance_id, {msg.dev_id});
		device.shm = shm::device{bip::open_only, shm::segment::remove_when_done, device_shmid};
		m.devices = m.devices.insert(device);
		return_fn({msg.dev_id}, true);
		DATA_->model.lock_write(m);
		DATA_->model.lock_publish(m); // Device may not have been published yet.
	}
	else {
		// The sandbox failed to create the remote device.
		const auto err = "Failed to create remote device.";
		m = set_error(std::move(m), {msg.dev_id}, err);
		return_fn({msg.dev_id}, false);
		DATA_->callbacks.on_device_error({msg.dev_id}, err);
		DATA_->model.lock_write(m);
	}
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::device_param_info_changed& msg) -> void {
	DATA_->callbacks.on_device_params_changed({msg.dev_id});
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::report_error& msg) -> void {
	DATA_->callbacks.on_sbox_error(sbox_id, msg.text.c_str());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::report_fatal_error& msg) -> void {
	// This message could be received if the sandbox process
	// manages to prematurely terminate itself in a "clean" way.
	DATA_->callbacks.on_sbox_crashed(sbox_id, msg.text.c_str());
	// TODO: terminate the sandbox process if it is still running and figure out what else needs to be done here.
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::report_info& msg) -> void {
	DATA_->callbacks.on_sbox_info(sbox_id, msg.text.c_str());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::report_warning& msg) -> void {
	DATA_->callbacks.on_sbox_warning(sbox_id, msg.text.c_str());
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_param_value& msg) -> void {
	const auto m         = DATA_->model.lock_read();
	const auto sbox      = m.sandboxes.at(sbox_id);
	const auto return_fn = sbox.service->return_buffers.doubles.take(msg.callback);
	return_fn(msg.value);
}

static
auto process_message_(id::sandbox sbox_id, const msg::out::return_param_value_text& msg) -> void {
	const auto m         = DATA_->model.lock_read();
	const auto sbox      = m.sandboxes.at(sbox_id);
	const auto return_fn = sbox.service->return_buffers.strings.take(msg.callback);
	return_fn(msg.text.c_str());
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
	auto next_gc = now + std::chrono::milliseconds{GC_INTERVAL_MS};
	while (!stop_token.stop_requested()) {
		now = std::chrono::steady_clock::now();
		if (now > next_gc) {
			DATA_->model.lock_gc();
			next_gc = now + std::chrono::milliseconds{GC_INTERVAL_MS};
		}
		process_sandbox_messages();
		std::this_thread::sleep_for(std::chrono::milliseconds{POLL_SLEEP_MS});
	}
}

[[nodiscard]] static
auto is_running(const sandbox& sbox) -> bool {
	return sbox.service->proc.running();
}

[[nodiscard]] static
auto is_running(id::sandbox sbox) -> bool {
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
auto connect(id::device dev_out_id, size_t port_out, id::device dev_in_id, size_t port_in) -> void {
	auto m              = DATA_->model.lock_read();
	const auto& dev_out = m.devices.at(dev_out_id);
	const auto& dev_in  = m.devices.at(dev_in_id);
	if (dev_out.sbox == dev_in.sbox) {
		// Devices are in the same sandbox
		const auto& sbox = m.sandboxes.at(dev_out.sbox);
		sbox.service->enqueue(scuff::msg::in::device_connect{dev_out_id.value, port_out, dev_in_id.value, port_in});
		return;
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
	group.cross_sbox_conns = group.cross_sbox_conns.set(id::device{dev_out_id}, id::device{dev_in_id});
	m.groups = m.groups.insert(group);
	DATA_->model.lock_write(m);
	DATA_->model.lock_publish(m);
}

[[nodiscard]] static
auto find(ext::id::plugin plugin_id) -> id::plugin {
	const auto m = DATA_->model.lock_read();
	for (const auto& plugin : m.plugins) {
		if (plugin.ext_id == plugin_id) {
			return plugin.id;
		}
	}
	return {};
}

[[nodiscard]] static
auto create_device(model&& m, id::device dev_id, const sandbox& sbox, plugin_type type, ext::id::plugin plugin_ext_id, id::plugin plugin_id, return_device return_fn) -> model {
	scuff::device dev;
	dev.id            = dev_id;
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
		scuff::DATA_->callbacks.on_device_error(dev.id, err);
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

[[nodiscard]] static
auto create_device(id::sandbox sbox_id, plugin_type type, ext::id::plugin plugin_ext_id, return_device fn) -> id::device {
	auto m               = DATA_->model.lock_read();
	const auto& sbox     = m.sandboxes.at(sbox_id);
	const auto plugin_id = id::plugin{find(plugin_ext_id)};
	const auto dev_id    = id::device{scuff::id_gen_++};
	m = create_device(std::move(m), dev_id, sbox, type, {plugin_ext_id}, plugin_id, fn);
	DATA_->model.lock_write(m);
	return dev_id;
}

static
auto device_disconnect(id::device dev_out_id, size_t port_out, id::device dev_in_id, size_t port_in) -> void {
	auto m              = DATA_->model.lock_read();
	const auto& dev_out = m.devices.at({dev_out_id});
	const auto& dev_in  = m.devices.at({dev_in_id});
	if (dev_out.sbox == dev_in.sbox) {
		// Devices are in the same sandbox.
		const auto& sbox = m.sandboxes.at(dev_out.sbox);
		sbox.service->enqueue(scuff::msg::in::device_disconnect{dev_out_id.value, port_out, dev_in_id.value, port_in});
		return;
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
	group.cross_sbox_conns = group.cross_sbox_conns.erase(id::device{dev_out_id});
	m.groups              = m.groups.insert(group);
	DATA_->model.lock_write(m);
	DATA_->model.lock_publish(m);
}

static
auto duplicate(id::device src_dev_id, id::sandbox dst_sbox_id, return_device fn) -> void {
	auto m                   = DATA_->model.lock_read();
	const auto src_dev       = m.devices.at({src_dev_id});
	const auto src_sbox      = m.sandboxes.at(src_dev.sbox);
	const auto dst_sbox      = m.sandboxes.at({dst_sbox_id});
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
				dst_sbox.service->enqueue(msg::in::device_load{dev_id.value, src_state});
			}
			// Call user's callback
			fn(dev_id, success);
		};
		auto m = DATA_->model.lock_read();
		const auto dev_id = id::device{scuff::id_gen_++};
		m = create_device(std::move(m), dev_id, dst_sbox, type, plugin_ext_id, plugin, return_fn);
		DATA_->model.lock_write(m);
	});
	src_sbox.service->enqueue(msg::in::device_save{src_dev_id.value, save_cb});
}

static
auto erase(id::device dev_id) -> void {
	auto m            = DATA_->model.lock_read();
	const auto& dev   = m.devices.at(dev_id);
	const auto& sbox  = m.sandboxes.at(dev.sbox);
	const auto& group = m.groups.at(sbox.group);
	for (const auto sbox_id : group.sandboxes) {
		const auto& sbox = m.sandboxes.at(sbox_id);
		sbox.service->enqueue(msg::in::device_erase{dev_id.value});
	}
	m = remove_device_from_sandbox(std::move(m), dev.sbox, dev_id);
	m.devices = m.devices.erase(dev_id);
	DATA_->model.lock_write(m);
	DATA_->model.lock_publish(m);
}

[[nodiscard]] static
auto get_error(id::device dev) -> const char* {
	return DATA_->model.lock_read().devices.at(dev).error->c_str();
}

[[nodiscard]] static
auto get_name(id::device dev) -> const char* {
	return DATA_->model.lock_read().devices.at(dev).name->c_str();
}

[[nodiscard]] static
auto get_param_count(id::device dev) -> size_t {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at(dev);
	if (!device.shm->is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return 0;
	}
	const auto lock = std::unique_lock{device.shm->data->param_info_mutex};
	return device.shm->data->param_info.size();
}

static
auto get_param_value_text(id::device dev, idx::param param, double value, return_string fn) -> void {
	const auto m        = DATA_->model.lock_read();
	const auto& device  = m.devices.at({dev});
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto callback = sbox.service->return_buffers.strings.put(fn);
	sbox.service->enqueue(msg::in::get_param_value_text{dev.value, param.value, value, callback});
}

[[nodiscard]] static
auto get_plugin(id::device dev) -> id::plugin {
	return DATA_->model.lock_read().devices.at(dev).plugin;
}

[[nodiscard]] static
auto has_gui(id::device dev) -> bool {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at(dev);
	if (!device.shm->is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return false;
	}
	const auto flags   = device.shm->data->flags;
	return flags.value & flags.has_gui;
}

[[nodiscard]] static
auto has_params(id::device dev) -> bool {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at(dev);
	if (!device.shm->is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return false;
	}
	const auto flags   = device.shm->data->flags;
	return flags.value & flags.has_params;
}

static
auto set_render_mode(id::device dev, render_mode mode) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at(dev);
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::device_set_render_mode{dev.value, mode});
}

static
auto gui_hide(id::device dev) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at(dev);
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::device_gui_hide{dev.value});
}

static
auto gui_show(id::device dev) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at(dev);
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::device_gui_show{dev.value});
}

[[nodiscard]] static
auto was_loaded_successfully(id::device dev) -> bool {
	return DATA_->model.lock_read().devices.at(dev).error->empty();
}

[[nodiscard]] static
auto create_group() -> id::group {
	scuff::group group;
	auto m   = DATA_->model.lock_read();
	group.id = id::group{id_gen_++};
	try {
		const auto shmid   = shm::group::make_id(DATA_->instance_id, group.id);
		group.service      = std::make_shared<group_service>();
		group.service->shm = shm::group{bip::create_only, shm::segment::remove_when_done, shmid};
	} catch (const std::exception& err) {
		DATA_->callbacks.on_error(err.what());
		return {};
	}
	m.groups = m.groups.insert(group);
	DATA_->model.lock_write(m);
	return {group.id.value};
}

static
auto erase(id::group group_id) -> void {
	auto m            = DATA_->model.lock_read();
	const auto& group = m.groups.at(group_id);
	for (const auto sbox_id : group.sandboxes) {
		const auto& sbox = m.sandboxes.at(sbox_id);
		if (sbox.service->proc.running()) {
			sbox.service->proc.terminate();
		}
	}
	// TODO: also erase sandboxes in group ??
	m.groups = m.groups.erase(group_id);
	DATA_->model.lock_write(m);
	DATA_->model.lock_publish(m);
}

static
auto is_scanning() -> bool {
	return scan_::is_running();
}

static
auto get_value(id::device dev, idx::param param, return_double fn) -> void {
	const auto& m       = DATA_->model.lock_read();
	const auto& device  = m.devices.at({dev});
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto callback = sbox.service->return_buffers.doubles.put(fn);
	sbox.service->enqueue(scuff::msg::in::get_param_value{dev.value, param.value, callback});
}

[[nodiscard]] static
auto find(id::device dev_id, uint32_t param_id) -> idx::param {
	const auto m    = DATA_->model.lock_read();
	const auto dev  = m.devices.at(dev_id);
	const auto lock = std::unique_lock{dev.shm->data->param_info_mutex};
	for (size_t i = 0; i < dev.shm->data->param_info.size(); i++) {
		const auto& info = dev.shm->data->param_info[i];
		if (info.id.value == param_id) {
			return {i};
		}
	}
	return {};
}

[[nodiscard]] static
auto get_info(id::device dev_id, idx::param param) -> param_info {
	const auto m    = DATA_->model.lock_read();
	const auto dev  = m.devices.at(dev_id);
	const auto lock = std::unique_lock{dev.shm->data->param_info_mutex};
	if (param.value >= dev.shm->data->param_info.size()) {
		throw std::runtime_error("Invalid parameter index.");
	}
	return dev.shm->data->param_info[param.value];
}

static
auto push_event(id::device dev, const scuff::event& event) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto& device = m.devices.at({dev});
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.service->enqueue(scuff::msg::in::event{dev.value, event});
}

[[nodiscard]] static
auto get_path(id::plugfile plugfile) -> const char* {
	return DATA_->model.lock_read().plugfiles.at({plugfile}).path->c_str();
}

[[nodiscard]] static
auto get_error(id::plugfile plugfile) -> const char* {
	return DATA_->model.lock_read().plugfiles.at({plugfile}).error->c_str();
}

[[nodiscard]] static
auto get_error(id::plugin plugin) -> const char* {
	return DATA_->model.lock_read().plugins.at({plugin}).error->c_str();
}

[[nodiscard]] static
auto get_ext_id(id::plugin plugin) -> ext::id::plugin {
	return DATA_->model.lock_read().plugins.at(plugin).ext_id;
}

[[nodiscard]] static
auto get_name(id::plugin plugin) -> const char* {
	return DATA_->model.lock_read().plugins.at({plugin}).name->c_str();
}

[[nodiscard]] static
auto get_vendor(id::plugin plugin) -> const char* {
	return DATA_->model.lock_read().plugins.at({plugin}).vendor->c_str();
}

[[nodiscard]] static
auto get_version(id::plugin plugin) -> const char* {
	return DATA_->model.lock_read().plugins.at({plugin}).version->c_str();
}

static
auto restart(id::sandbox sbox, const char* sbox_exe_path) -> void {
	const auto m       = DATA_->model.lock_read();
	const auto sandbox = m.sandboxes.at({sbox});
	const auto& group  = m.groups.at(sandbox.group);
	if (sandbox.service->proc.running()) {
		sandbox.service->proc.terminate();
	}
	const auto group_shmid   = group.service->shm.id();
	const auto sandbox_shmid = sandbox.service->get_shmid();
	const auto exe_args      = make_sbox_exe_args(group_shmid, sandbox_shmid);
	sandbox.service->proc   = bp::child{sbox_exe_path, exe_args};
	// TODO: the rest of this
}

static
auto do_scan(const char* scan_exe_path, int flags) -> void {
	scan_::stop_if_it_is_already_running();
	scan_::start(scan_exe_path, flags);
}

[[nodiscard]] static
auto create_sandbox(id::group group_id, const char* sbox_exe_path) -> id::sandbox {
	sandbox sbox;
	sbox.id = id::sandbox{id_gen_++};
	auto m  = DATA_->model.lock_read();
	try {
		const auto& group        = m.groups.at({group_id});
		const auto group_shmid   = group.service->shm.id();
		const auto sandbox_shmid = shm::sandbox::make_id(DATA_->instance_id, sbox.id);
		const auto exe_args      = make_sbox_exe_args(group_shmid, sandbox_shmid);
		auto proc                = bp::child{sbox_exe_path, exe_args};
		sbox.group               = {group_id};
		sbox.service            = std::make_shared<sandbox_service>(std::move(proc), sandbox_shmid);
		// Add sandbox to group
		m = add_sandbox_to_group(std::move(m), {group_id}, sbox.id);
	}
	catch (const std::exception& err) {
		sbox.error = err.what();
		DATA_->callbacks.on_sbox_error(sbox.id, err.what());
	}
	m.sandboxes = m.sandboxes.insert(sbox);
	DATA_->model.lock_write(m);
	DATA_->model.lock_publish(m);
	return sbox.id;
}

static
auto erase(id::sandbox sbox) -> void {
	auto m              = DATA_->model.lock_read();
	const auto& sandbox = m.sandboxes.at({sbox});
	m = remove_sandbox_from_group(std::move(m), sandbox.group, {sbox});
	m.sandboxes = m.sandboxes.erase({sbox});
	DATA_->model.lock_write(m);
	DATA_->model.lock_publish(m);
}

[[nodiscard]] static
auto get_error(id::sandbox sbox) -> const char* {
	return DATA_->model.lock_read().sandboxes.at(sbox).error->c_str();
}

static
auto set_sample_rate(double sr) -> void {
	const auto m = DATA_->model.lock_read();
	for (const auto& sbox : m.sandboxes) {
		if (sbox.service->proc.running()) {
			sbox.service->enqueue(msg::in::set_sample_rate{sr});
		}
	}
}

} // impl

namespace scuff {

auto audio_process(group_process process) -> void {
	const auto audio     = scuff::DATA_->model.lockfree_read();
	const auto& group    = audio->groups.at({process.group});
	const auto epoch     = ++group.service->epoch;
	impl::write_entry_ports(*audio, process.input_devices);
	if (impl::do_sandbox_processing(group, epoch)) {
		impl::read_exit_ports(*audio, process.output_devices);
	}
	else {
		impl::read_zeros(*audio, process.output_devices);
	}
}

auto init(const scuff::config* config) -> bool {
	if (scuff::initialized_) { return true; }
	scuff::DATA_              = std::make_unique<scuff::data>();
	scuff::DATA_->callbacks   = config->callbacks;
	scuff::DATA_->instance_id = "scuff+" + std::to_string(scuff::os::get_process_id());
	try {
		scuff::DATA_->poll_thread = std::jthread{impl::poll_thread};
		scuff::initialized_       = true;
		return true;
	} catch (const std::exception& err) {
		scuff::DATA_.reset();
		config->callbacks.on_error(err.what());
		return false;
	}
}

auto shutdown() -> void {
	if (!scuff::initialized_) { return; }
	scuff::DATA_->poll_thread.request_stop();
	scuff::DATA_->poll_thread.join();
	scuff::DATA_.reset();
	scuff::initialized_ = false;
}

auto close_all_editors() -> void {
	try                               { impl::close_all_editors(); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto connect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void {
	try                               { impl::connect(dev_out, port_out, dev_in, port_in); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto create_device(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id, return_device fn) -> id::device {
	try                               { return impl::create_device(sbox, type, plugin_id, fn); }
	catch (const std::exception& err) { impl::report_error(err.what()); return {}; }
}

auto disconnect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void {
	try                               { impl::device_disconnect(dev_out, port_out, dev_in, port_in); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto duplicate(id::device dev, id::sandbox sbox, return_device fn) -> void {
	try                               { impl::duplicate(dev, sbox, fn); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto erase(id::device dev) -> void {
	try                               { impl::erase({dev}); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto get_error(id::device device) -> const char* {
	try                               { return impl::get_error(device); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto get_name(id::device dev) -> const char* {
	try                               { return impl::get_name(dev); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto get_param_count(id::device dev) -> size_t {
	try                               { return impl::get_param_count(dev); }
	catch (const std::exception& err) { impl::report_error(err.what()); return 0; }
}

auto get_param_value_text(id::device dev, idx::param param, double value, return_string fn) -> void {
	try                               { impl::get_param_value_text(dev, param, value, fn); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto get_plugin(id::device dev) -> id::plugin {
	try                               { return impl::get_plugin(dev); }
	catch (const std::exception& err) { impl::report_error(err.what()); return {}; }
}

auto has_gui(id::device dev) -> bool {
	try                               { return impl::has_gui(dev); }
	catch (const std::exception& err) { impl::report_error(err.what()); return false; }
}

auto has_params(id::device dev) -> bool {
	try                               { return impl::has_params(dev); }
	catch (const std::exception& err) { impl::report_error(err.what()); return false; }
}

auto set_render_mode(id::device dev, render_mode mode) -> void {
	try                               { impl::set_render_mode(dev, mode); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto gui_hide(id::device dev) -> void {
	try                               { impl::gui_hide(dev); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto gui_show(id::device dev) -> void {
	try                               { impl::gui_show(dev); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto was_loaded_successfully(id::device dev) -> bool {
	try                               { return impl::was_loaded_successfully(dev); }
	catch (const std::exception& err) { impl::report_error(err.what()); return false; }
}

auto create_group() -> id::group {
	try                               { return impl::create_group(); }
	catch (const std::exception& err) { impl::report_error(err.what()); return {}; }
}

auto erase(id::group group) -> void {
	try                               { impl::erase({group}); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto is_running(id::sandbox sbox) -> bool {
	try                               { return impl::is_running(sbox); }
	catch (const std::exception& err) { impl::report_error(err.what()); return false; }
}

auto is_scanning() -> bool {
	try                               { return impl::is_scanning(); }
	catch (const std::exception& err) { impl::report_error(err.what()); return false; }
}

auto get_value(id::device dev, idx::param param, return_double fn) -> void {
	try                               { impl::get_value(dev, param, fn); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto get_info(id::device dev, idx::param param) -> param_info {
	try                               { return impl::get_info({dev}, param); }
	catch (const std::exception& err) { impl::report_error(err.what()); return {0}; }
}

auto push_event(id::device dev, const scuff::event& event) -> void {
	try                               { impl::push_event(dev, event); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto find(ext::id::plugin plugin_id) -> id::plugin {
	try                               { return impl::find(plugin_id); }
	catch (const std::exception& err) { impl::report_error(err.what()); return {}; }
}

auto get_path(id::plugfile plugfile) -> const char* {
	try                               { return impl::get_path(plugfile); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto get_error(id::plugfile plugfile) -> const char* {
	try                               { return impl::get_error(plugfile); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto get_error(id::plugin plugin) -> const char* {
	try                               { return impl::get_error(plugin); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto get_ext_id(id::plugin plugin) -> ext::id::plugin {
	try                               { return impl::get_ext_id(plugin); }
	catch (const std::exception& err) { impl::report_error(err.what()); return {}; }
}

auto get_name(id::plugin plugin) -> const char* {
	try                               { return impl::get_name(plugin); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto get_vendor(id::plugin plugin) -> const char* {
	try                               { return impl::get_vendor(plugin); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto get_version(id::plugin plugin) -> const char* {
	try                               { return impl::get_version(plugin); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto restart(id::sandbox sbox, const char* sbox_exe_path) -> void {
	try                               { impl::restart(sbox, sbox_exe_path); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto scan(const char* scan_exe_path, int flags) -> void {
	try                               { impl::do_scan(scan_exe_path, flags); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto create_sandbox(id::group group_id, const char* sbox_exe_path) -> id::sandbox {
	try                               { return impl::create_sandbox(group_id, sbox_exe_path); }
	catch (const std::exception& err) { impl::report_error(err.what()); return {}; }
}

auto erase(id::sandbox sbox) -> void {
	try                               { impl::erase(sbox); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

auto get_error(id::sandbox sbox) -> const char* {
	try                               { return impl::get_error(sbox); }
	catch (const std::exception& err) { impl::report_error(err.what()); return ""; }
}

auto set_sample_rate(double sr) -> void {
	try                               { impl::set_sample_rate(sr); }
	catch (const std::exception& err) { impl::report_error(err.what()); }
}

} // scuff
