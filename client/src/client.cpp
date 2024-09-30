#include "client.hpp"
#include "common/speen.hpp"
#include "common/types.hpp"
#include "common/visit.hpp"
#include "managed.hpp"
#include "scan.hpp"
#include <clap/plugin-features.h>
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
	for (size_t j = 0; j < writers.size(); j++) {
		const auto& writer = writers[j];
		auto& buffer       = dev.services->shm.data->audio_in[writer.port_index];
		writer.write(buffer.data());
	}
}

static
auto write_events(const scuff::device& dev, const event_writer& writer) -> void {
	auto& buffer           = dev.services->shm.data->events_in;
	const auto event_count = std::min(writer.count(), size_t(EVENT_PORT_SIZE));
	for (size_t j = 0; j < event_count; j++) {
		buffer.push_back(writer.get(j));
	}
}

static
auto write_entry_ports(const scuff::model& m, const input_devices& devices) -> void {
	for (size_t i = 0; i < devices.size(); i++) {
		const auto& item  = devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		write_audio(dev, item.audio_writers);
		write_events(dev, item.event_writer);
	}
}

static
auto read_audio(const scuff::device& dev, const audio_readers& readers) -> void {
	for (size_t j = 0; j < readers.size(); j++) {
		const auto& reader = readers[j];
		const auto& buffer = dev.services->shm.data->audio_out[reader.port_index];
		reader.read(buffer.data());
	}
}

static
auto read_zeros(const scuff::device& dev, const audio_readers& readers) -> void {
	std::array<float, CHANNEL_COUNT * VECTOR_SIZE> zeros = {0.0f};
	for (size_t j = 0; j < readers.size(); j++) {
		const auto& reader = readers[j];
		reader.read(zeros.data());
	}
}

static
auto read_events(const scuff::device& dev, const event_reader& reader) -> void {
	auto& buffer           = dev.services->shm.data->events_out;
	const auto event_count = buffer.size();
	for (size_t j = 0; j < event_count; j++) {
		reader.push(buffer[j]);
	}
	buffer.clear();
}

static
auto read_exit_ports(const scuff::model& m, const output_devices& devices) -> void {
	for (size_t i = 0; i < devices.size(); i++) {
		const auto& item  = devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		read_audio(dev, item.audio_readers);
		read_events(dev, item.event_reader);
	}
}

static
auto read_zeros(const scuff::model& m, const output_devices& devices) -> void {
	for (size_t i = 0; i < devices.size(); i++) {
		const auto& item  = devices[i];
		const auto dev_id = id::device{item.dev};
		const auto& dev   = m.devices.at(dev_id);
		read_zeros(dev, item.audio_readers);
	}
}

static
auto signal_sandbox_processing(const scuff::group& group, uint64_t epoch) -> void {
	auto& data = *group.services->shm.data;
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
	auto& data = *group.services->shm.data;
	auto done = [&data]() -> bool {
		return data.sandboxes_processing.load(std::memory_order_acquire) < 1;
	};
	if (done()) {
		return true;
	}
	auto lock = std::unique_lock{data.mut};
	return group.services->shm.data->cv.wait_for(lock, MAX_WAIT_TIME, done);
}

[[nodiscard]] static
auto do_sandbox_processing(const scuff::group& group, uint64_t epoch) -> bool {
	signal_sandbox_processing(group, epoch);
	return wait_for_all_sandboxes_done(group);
}

static
auto process_message_(const sandbox& sbox, const msg::out::return_created_device& msg) -> void {
	DATA_->model.update_publish([sbox, msg](model&& m){
		const auto return_fn = sbox.services->return_buffers.devices.take(msg.callback);
		if (!msg.ports_shmid.empty()) {
			// The sandbox succeeded in creating the remote device.
			auto device             = m.devices.at({msg.dev_id});
			const auto device_shmid = shm::device::make_id(DATA_->instance_id, {msg.dev_id});
			device.services         = std::make_shared<device_services>();
			device.services->shm    = shm::device{bip::open_only, shm::segment::remove_when_done, device_shmid};
			m.devices = m.devices.insert(device);
			return_fn({msg.dev_id}, true);
			return m;
		}
		// The sandbox failed to create the remote device.
		const auto err = "Failed to create remote device.";
		m = set_error(std::move(m), {msg.dev_id}, err);
		return_fn({msg.dev_id}, false);
		report::send(sbox, report::msg::device_error{{msg.dev_id}, err});
		return m;
	});
}

static
auto process_message_(const sandbox& sbox, const msg::out::device_param_info_changed& msg) -> void {
	report::send(sbox, report::msg::device_params_changed{{msg.dev_id}});
}

static
auto process_message_(const sandbox& sbox, const msg::out::report_error& msg) -> void {
	report::send(sbox, report::msg::sbox_error{sbox.id, msg.text});
}

static
auto process_message_(const sandbox& sbox, const msg::out::report_fatal_error& msg) -> void {
	// This message could be received if the sandbox process
	// manages to prematurely terminate itself in a "clean" way.
	report::send(sbox, report::msg::sbox_crashed{sbox.id, msg.text});
	// TODO: terminate the sandbox process if it is still running and figure out what else needs to be done here.
}

static
auto process_message_(const sandbox& sbox, const msg::out::report_info& msg) -> void {
	report::send(sbox, report::msg::sbox_info{sbox.id, msg.text});
}

static
auto process_message_(const sandbox& sbox, const msg::out::report_warning& msg) -> void {
	report::send(sbox, report::msg::sbox_warning{sbox.id, msg.text});
}

static
auto process_message_(const sandbox& sbox, const msg::out::return_param_value& msg) -> void {
	const auto return_fn = sbox.services->return_buffers.doubles.take(msg.callback);
	return_fn(msg.value);
}

static
auto process_message_(const sandbox& sbox, const msg::out::return_param_value_text& msg) -> void {
	const auto return_fn = sbox.services->return_buffers.strings.take(msg.callback);
	return_fn(msg.text.c_str());
}

static
auto process_message_(const sandbox& sbox, const msg::out::return_state& msg) -> void {
	const auto return_fn = sbox.services->return_buffers.states.take(msg.callback);
	return_fn(msg.bytes);
}

static
auto process_message(const sandbox& sbox, const msg::out::msg& msg) -> void {
	 const auto proc = [sbox](const auto& msg) -> void { process_message_(sbox, msg); };
	 try                               { fast_visit(proc, msg); }
	 catch (const std::exception& err) { report::send(sbox, report::msg::error{err.what()}); }
}

static
auto process_sandbox_messages(const sandbox& sbox) -> void {
	if (!sbox.services->proc.running()) {
		// The sandbox process has stopped unexpectedly.
		// TODO: Handle this situation
		return;
	}
	sbox.services->send_msgs();
	const auto msgs = sbox.services->receive_msgs();
	for (const auto& msg : msgs) {
		process_message(sbox, msg);
	}
}

static
auto process_sandbox_messages() -> void {
	const auto sandboxes = DATA_->model.read().sandboxes;
	for (const auto& sbox : sandboxes) {
		process_sandbox_messages(sbox);
	}
}

template <typename T> [[nodiscard]] static
auto pop_report_msg(report::msg::q<T>* reporter) -> std::optional<T> {
	const auto q   = reporter->lock();
	if (q->empty()) {
		return std::nullopt;
	}
	const auto msg = q->front();
	q->pop_front();
	return msg;
}

static auto return_report_msg_(const report::msg::error& msg, const general_reporter& reporter) -> void { reporter.on_error(msg.error); } 
static auto return_report_msg_(const report::msg::plugfile_broken& msg, const general_reporter& reporter) -> void { reporter.on_plugfile_broken(msg.plugfile); }
static auto return_report_msg_(const report::msg::plugfile_scanned& msg, const general_reporter& reporter) -> void { reporter.on_plugfile_scanned(msg.plugfile); }
static auto return_report_msg_(const report::msg::plugin_broken& msg, const general_reporter& reporter) -> void { reporter.on_plugin_broken(msg.plugin); }
static auto return_report_msg_(const report::msg::plugin_scanned& msg, const general_reporter& reporter) -> void { reporter.on_plugin_scanned(msg.plugin); }
static auto return_report_msg_(const report::msg::scan_complete& msg, const general_reporter& reporter) -> void { reporter.on_scan_complete(); }
static auto return_report_msg_(const report::msg::scan_error& msg, const general_reporter& reporter) -> void { reporter.on_scan_error(msg.error); }
static auto return_report_msg_(const report::msg::scan_started& msg, const general_reporter& reporter) -> void { reporter.on_scan_started(); }
static auto return_report_msg_(const report::msg::device_error& msg, const group_reporter& reporter) -> void { reporter.on_device_error(msg.dev, msg.error); }
static auto return_report_msg_(const report::msg::device_params_changed& msg, const group_reporter& reporter) -> void { reporter.on_device_params_changed(msg.dev); }
static auto return_report_msg_(const report::msg::error& msg, const group_reporter& reporter) -> void { reporter.on_error(msg.error); }
static auto return_report_msg_(const report::msg::sbox_crashed& msg, const group_reporter& reporter) -> void { reporter.on_sbox_crashed(msg.sbox, msg.error); }
static auto return_report_msg_(const report::msg::sbox_error& msg, const group_reporter& reporter) -> void { reporter.on_sbox_error(msg.sbox, msg.error); }
static auto return_report_msg_(const report::msg::sbox_info& msg, const group_reporter& reporter) -> void { reporter.on_sbox_info(msg.sbox, msg.info); }
static auto return_report_msg_(const report::msg::sbox_started& msg, const group_reporter& reporter) -> void { reporter.on_sbox_started(msg.sbox); }
static auto return_report_msg_(const report::msg::sbox_warning& msg, const group_reporter& reporter) -> void { reporter.on_sbox_warning(msg.sbox, msg.warning); }

static
auto return_report_msg(const report::msg::general& msg, const general_reporter& reporter) -> void {
	fast_visit([&reporter](const auto& msg) { return_report_msg_(msg, reporter); }, msg);
}

static
auto return_report_msg(const report::msg::group& msg, const group_reporter& reporter) -> void {
	fast_visit([&reporter](const auto& msg) { return_report_msg_(msg, reporter); }, msg);
}

static
auto receive_report(const general_reporter& reporter) -> void {
	while (const auto msg = pop_report_msg(&DATA_->reporter)) {
		return_report_msg(*msg, reporter);
	}
}

static
auto receive_report(scuff::id::group group_id, const group_reporter& reporter) -> void {
	const auto m      = DATA_->model.read();
	const auto& group = m.groups.at(group_id);
	while (const auto msg = pop_report_msg(&group.services->reporter)) {
		return_report_msg(*msg, reporter);
	}
}

static
auto poll_thread(std::stop_token stop_token) -> void {
	auto now     = std::chrono::steady_clock::now();
	auto next_gc = now + std::chrono::milliseconds{GC_INTERVAL_MS};
	while (!stop_token.stop_requested()) {
		now = std::chrono::steady_clock::now();
		if (now > next_gc) {
			DATA_->model.gc();
			next_gc = now + std::chrono::milliseconds{GC_INTERVAL_MS};
		}
		process_sandbox_messages();
		std::this_thread::sleep_for(std::chrono::milliseconds{POLL_SLEEP_MS});
	}
}

[[nodiscard]] static
auto is_running(const sandbox& sbox) -> bool {
	return sbox.services->proc.running();
}

[[nodiscard]] static
auto is_running(id::sandbox sbox) -> bool {
	return is_running(scuff::DATA_->model.read().sandboxes.at({sbox}));
}

static
auto activate(id::group group, double sr) -> void {
	// TODO: impl::activate
}

static
auto deactivate(id::group group) -> void {
	// TODO: impl::deactivate
}

static
auto close_all_editors() -> void {
	const auto sandboxes = scuff::DATA_->model.read().sandboxes;
	for (const auto& sandbox : sandboxes) {
		if (is_running(sandbox)) {
			sandbox.services->enqueue(scuff::msg::in::close_all_editors{});
		}
	}
}

static
auto connect(id::device dev_out_id, size_t port_out, id::device dev_in_id, size_t port_in) -> void {
	DATA_->model.update_publish([dev_out_id, port_out, dev_in_id, port_in](model&& m){
		const auto& dev_out = m.devices.at(dev_out_id);
		const auto& dev_in  = m.devices.at(dev_in_id);
		if (dev_out.sbox == dev_in.sbox) {
			// Devices are in the same sandbox
			const auto& sbox = m.sandboxes.at(dev_out.sbox);
			sbox.services->enqueue(scuff::msg::in::device_connect{dev_out_id.value, port_out, dev_in_id.value, port_in});
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
		group.cross_sbox_conns = group.cross_sbox_conns.set(id::device{dev_out_id}, id::device{dev_in_id});
		m.groups = m.groups.insert(group);
		return m;
	});
}

[[nodiscard]] static
auto find(ext::id::plugin plugin_id) -> id::plugin {
	const auto m = DATA_->model.read();
	for (const auto& plugin : m.plugins) {
		if (plugin.ext_id == plugin_id) {
			return plugin.id;
		}
	}
	return {};
}

[[nodiscard]] static
auto create_device_async(model&& m, id::device dev_id, const sandbox& sbox, plugin_type type, ext::id::plugin plugin_ext_id, id::plugin plugin_id, return_device return_fn) -> model {
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
		report::send(sbox, report::msg::device_error{dev.id, err});
		return_fn(dev.id, false);
		return m;
	}
	// Plugin is available so we need to send a message to the sandbox to create the remote device.
	const auto callback = sbox.services->return_buffers.devices.put(return_fn);
	const auto plugin   = m.plugins.at(dev.plugin);
	const auto plugfile = m.plugfiles.at(plugin.plugfile);
	sbox.services->enqueue(msg::in::device_create{dev.id.value, type, plugfile.path, plugin_ext_id.value, callback});
	return m;
}

[[nodiscard]] static
auto create_device_async(id::sandbox sbox_id, plugin_type type, ext::id::plugin plugin_ext_id, return_device fn) -> id::device {
	const auto dev_id = id::device{scuff::id_gen_++};
	DATA_->model.update([dev_id, sbox_id, type, plugin_ext_id, fn](model&& m){
		const auto sbox     = m.sandboxes.at(sbox_id);
		const auto plugin_id = id::plugin{find(plugin_ext_id)};
		m = create_device_async(std::move(m), dev_id, sbox, type, plugin_ext_id, plugin_id, fn);
		return m;
	});
	return dev_id;
}

[[nodiscard]] static
auto create_device(id::sandbox sbox_id, plugin_type type, ext::id::plugin plugin_ext_id) -> id::device {
	static constexpr auto MAX_WAIT = std::chrono::seconds(1);
	std::condition_variable cv;
	std::mutex mutex;
	bool done = false;
	auto fn = [&cv, &mutex, &done](id::device dev_id, bool load_success) -> void {
		std::lock_guard lock{mutex};
		done = true;
		cv.notify_one();
	};
	auto ready = [&done] {
		return !done;
	};
	const auto dev_id = impl::create_device_async(sbox_id, type, plugin_ext_id, fn);
	auto lock = std::unique_lock{mutex};
	if (!cv.wait_for(lock, MAX_WAIT, ready)) {
		report::send(report::msg::error{"Timed out waiting for device creation"});
	}
	if (!done) {
		return {};
	}
	return dev_id;
}

static
auto device_disconnect(id::device dev_out_id, size_t port_out, id::device dev_in_id, size_t port_in) -> void {
	DATA_->model.update_publish([dev_out_id, port_out, dev_in_id, port_in](model&& m){
		const auto& dev_out = m.devices.at({dev_out_id});
		const auto& dev_in  = m.devices.at({dev_in_id});
		if (dev_out.sbox == dev_in.sbox) {
			// Devices are in the same sandbox.
			const auto& sbox = m.sandboxes.at(dev_out.sbox);
			sbox.services->enqueue(scuff::msg::in::device_disconnect{dev_out_id.value, port_out, dev_in_id.value, port_in});
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
		group.cross_sbox_conns = group.cross_sbox_conns.erase(id::device{dev_out_id});
		m.groups              = m.groups.insert(group);
		return m;
	});
}

static
auto duplicate_async(id::device src_dev_id, id::sandbox dst_sbox_id, return_device fn) -> id::device {
	auto m                   = DATA_->model.read();
	const auto new_dev_id    = id::device{scuff::id_gen_++};
	const auto src_dev       = m.devices.at({src_dev_id});
	const auto src_sbox      = m.sandboxes.at(src_dev.sbox);
	const auto dst_sbox      = m.sandboxes.at({dst_sbox_id});
	const auto plugin_ext_id = src_dev.plugin_ext_id;
	const auto plugin        = src_dev.plugin;
	const auto type          = src_dev.type;
	// We're going to send a message to the source sandbox to save the source device.
	// When the saved state is returned, call this function with it:
	const auto save_cb = src_sbox.services->return_buffers.states.put([=](const std::vector<std::byte>& src_state) {
		// Now we're going to send a message to the destination sandbox to actually create the new device,
		// if the plugin is available.
		// When the new device is created, call this function with it:
		const auto return_fn = [=](id::device dev_id, bool success) {
			if (success) {
				// Remote device was created successfully.
				// Now send a message to the destination sandbox to load the saved state into the new device.
				dst_sbox.services->enqueue(msg::in::device_load{dev_id.value, src_state});
			}
			// Call user's callback
			fn(dev_id, success);
		};
		DATA_->model.update([=](model&& m){
			return create_device_async(std::move(m), new_dev_id, dst_sbox, type, plugin_ext_id, plugin, return_fn);
		});
	});
	src_sbox.services->enqueue(msg::in::device_save{src_dev_id.value, save_cb});
	return new_dev_id;
}

static
auto duplicate(id::device src_dev_id, id::sandbox dst_sbox_id) -> id::device {
	// TODO: blocking duplicate
	return {};
}

static
auto erase(id::device dev_id) -> void {
	DATA_->model.update_publish([=](model&& m){
		const auto& dev   = m.devices.at(dev_id);
		const auto& sbox  = m.sandboxes.at(dev.sbox);
		const auto& group = m.groups.at(sbox.group);
		for (const auto sbox_id : group.sandboxes) {
			const auto& sbox = m.sandboxes.at(sbox_id);
			sbox.services->enqueue(msg::in::device_erase{dev_id.value});
		}
		m = remove_device_from_sandbox(std::move(m), dev.sbox, dev_id);
		m.devices = m.devices.erase(dev_id);
		return m;
	});
}

[[nodiscard]] static
auto find(id::device dev, ext::id::param param_id) -> idx::param {
	// TODO: impl::find
	return {};
}

[[nodiscard]] static
auto find(id::device dev_id, uint32_t param_id) -> idx::param {
	const auto m    = DATA_->model.read();
	const auto dev  = m.devices.at(dev_id);
	const auto lock = std::unique_lock{dev.services->shm.data->param_info_mutex};
	for (size_t i = 0; i < dev.services->shm.data->param_info.size(); i++) {
		const auto& info = dev.services->shm.data->param_info[i];
		if (info.id.value == param_id) {
			return {i};
		}
	}
	return {};
}

[[nodiscard]] static
auto get_error(id::device dev) -> const char* {
	return DATA_->model.read().devices.at(dev).error->c_str();
}

[[nodiscard]] static
auto get_features(id::plugin plugin) -> std::vector<std::string> {
	const auto list = DATA_->model.read().plugins.at(plugin).clap_features;
	std::vector<std::string> out;
	for (const auto& feature : list) {
		out.push_back(feature);
	}
	return out;
}

[[nodiscard]] static
auto get_name(id::device dev) -> const char* {
	return DATA_->model.read().devices.at(dev).name->c_str();
}

[[nodiscard]] static
auto get_param_count(id::device dev) -> size_t {
	const auto m       = DATA_->model.read();
	const auto& device = m.devices.at(dev);
	if (!device.services->shm.is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return 0;
	}
	const auto lock = std::unique_lock{device.services->shm.data->param_info_mutex};
	return device.services->shm.data->param_info.size();
}

static
auto get_param_value_text(id::device dev, idx::param param, double value, return_string fn) -> void {
	const auto m        = DATA_->model.read();
	const auto& device  = m.devices.at({dev});
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto callback = sbox.services->return_buffers.strings.put(fn);
	sbox.services->enqueue(msg::in::get_param_value_text{dev.value, param.value, value, callback});
}

[[nodiscard]] static
auto get_plugin(id::device dev) -> id::plugin {
	return DATA_->model.read().devices.at(dev).plugin;
}

[[nodiscard]] static
auto get_type(id::plugin id) -> plugin_type {
	return DATA_->model.read().plugins.at(id).type;
}

[[nodiscard]] static
auto has_gui(id::device dev) -> bool {
	const auto m       = DATA_->model.read();
	const auto& device = m.devices.at(dev);
	if (!device.services->shm.is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return false;
	}
	const auto flags   = device.services->shm.data->flags;
	return flags.value & flags.has_gui;
}

[[nodiscard]] static
auto has_params(id::device dev) -> bool {
	const auto m       = DATA_->model.read();
	const auto& device = m.devices.at(dev);
	if (!device.services->shm.is_valid()) {
		// Device wasn't successfully created by the sandbox process (yet?)
		return false;
	}
	const auto flags   = device.services->shm.data->flags;
	return flags.value & flags.has_params;
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
auto has_rack_features(id::plugin id) -> bool {
	const auto m       = DATA_->model.read();
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
auto set_render_mode(id::device dev, render_mode mode) -> void {
	const auto m       = DATA_->model.read();
	const auto& device = m.devices.at(dev);
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.services->enqueue(scuff::msg::in::device_set_render_mode{dev.value, mode});
}

[[nodiscard]] static
auto get_broken_plugfiles() -> std::vector<id::plugfile> {
	std::vector<id::plugfile> out;
	const auto m = DATA_->model.read();
	for (const auto& pf : m.plugfiles) {
		if (!pf.error->empty()) {
			out.push_back(pf.id);
		}
	}
	return out;
}

[[nodiscard]] static
auto get_broken_plugins() -> std::vector<id::plugin> {
	std::vector<id::plugin> out;
	const auto m = DATA_->model.read();
	for (const auto& plugin : m.plugins) {
		if (!plugin.error->empty()) {
			out.push_back(plugin.id);
		}
	}
	return out;
}

static
auto gui_hide(id::device dev) -> void {
	const auto m       = DATA_->model.read();
	const auto& device = m.devices.at(dev);
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.services->enqueue(scuff::msg::in::device_gui_hide{dev.value});
}

static
auto gui_show(id::device dev) -> void {
	const auto m       = DATA_->model.read();
	const auto& device = m.devices.at(dev);
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.services->enqueue(scuff::msg::in::device_gui_show{dev.value});
}

[[nodiscard]] static
auto was_loaded_successfully(id::device dev) -> bool {
	return DATA_->model.read().devices.at(dev).error->empty();
}

[[nodiscard]] static
auto create_group(int flags) -> id::group {
	const auto group_id = id::group{id_gen_++};
	DATA_->model.update([=](model&& m){
		scuff::group group;
		group.id    = group_id;
		group.flags = flags;
		try {
			const auto shmid    = shm::group::make_id(DATA_->instance_id, group.id);
			group.services      = std::make_shared<group_services>();
			group.services->shm = shm::group{bip::create_only, shm::segment::remove_when_done, shmid};
		} catch (const std::exception& err) {
			report::send(report::msg::error{err.what()});
			return m;
		}
		m.groups = m.groups.insert(group);
		return m;
	});
	if (DATA_->model.read().groups.count(group_id) == 0) {
		// Failed to create the group.
		return {};
	}
	return group_id;
}

static
auto erase(id::group group_id) -> void {
	DATA_->model.update_publish([=](model&& m){
		const auto& group = m.groups.at(group_id);
		for (const auto sbox_id : group.sandboxes) {
			const auto& sbox = m.sandboxes.at(sbox_id);
			if (sbox.services->proc.running()) {
				sbox.services->proc.terminate();
			}
		}
		// TODO: also erase sandboxes in group ??
		m.groups = m.groups.erase(group_id);
		return m;
	});
}

static
auto is_scanning() -> bool {
	return scan_::is_running();
}

static
auto get_value_async(id::device dev, idx::param param, return_double fn) -> void {
	const auto& m       = DATA_->model.read();
	const auto& device  = m.devices.at({dev});
	const auto& sbox    = m.sandboxes.at(device.sbox);
	const auto callback = sbox.services->return_buffers.doubles.put(fn);
	sbox.services->enqueue(scuff::msg::in::get_param_value{dev.value, param.value, callback});
}

static
auto get_value(id::device dev, idx::param param) -> double {
	// TODO: impl::get_value
	return {};
}

[[nodiscard]] static
auto get_info(id::device dev_id, idx::param param) -> param_info {
	const auto m    = DATA_->model.read();
	const auto dev  = m.devices.at(dev_id);
	const auto lock = std::unique_lock{dev.services->shm.data->param_info_mutex};
	if (param.value >= dev.services->shm.data->param_info.size()) {
		throw std::runtime_error("Invalid parameter index.");
	}
	return dev.services->shm.data->param_info[param.value];
}

static
auto push_event(id::device dev, const scuff::event& event) -> void {
	const auto m       = DATA_->model.read();
	const auto& device = m.devices.at({dev});
	const auto& sbox   = m.sandboxes.at(device.sbox);
	sbox.services->enqueue(scuff::msg::in::event{dev.value, event});
}

[[nodiscard]] static
auto get_path(id::plugfile plugfile) -> const char* {
	return DATA_->model.read().plugfiles.at({plugfile}).path->c_str();
}

[[nodiscard]] static
auto get_error(id::plugfile plugfile) -> const char* {
	return DATA_->model.read().plugfiles.at({plugfile}).error->c_str();
}

[[nodiscard]] static
auto get_error(id::plugin plugin) -> const char* {
	return DATA_->model.read().plugins.at({plugin}).error->c_str();
}

[[nodiscard]] static
auto get_ext_id(id::plugin plugin) -> ext::id::plugin {
	return DATA_->model.read().plugins.at(plugin).ext_id;
}

[[nodiscard]] static
auto get_metadata(id::device dev_id, size_t column) -> std::any {
	const auto m    = DATA_->model.read();
	const auto& dev = m.devices.at(dev_id);
	return dev.metadata.at(column);
}

auto set_metadata(id::device dev_id, size_t column, std::any data) -> void {
	DATA_->model.update([=](model&& m){
		auto dev = m.devices.at(dev_id);
		while (dev.metadata.size() <= column) {
			dev.metadata = dev.metadata.push_back({});
		}
		dev.metadata = dev.metadata.set(column, std::move(data));
		return m;
	});
}

[[nodiscard]] static
auto get_name(id::plugin plugin) -> const char* {
	return DATA_->model.read().plugins.at({plugin}).name->c_str();
}

[[nodiscard]] static
auto get_plugin_ext_id(id::device dev) -> ext::id::plugin {
	return DATA_->model.read().devices.at(dev).plugin_ext_id;
}

[[nodiscard]] static
auto get_value_text(id::device dev_id, idx::param param, double value) -> std::string {
	static constexpr auto MAX_WAIT = std::chrono::seconds(1);
	std::condition_variable cv;
	std::mutex mutex;
	std::string result;
	auto fn = [&cv, &mutex, &result](std::string_view text) -> void {
		std::lock_guard lock{mutex};
		result = text;
		cv.notify_one();
	};
	auto ready = [&result] {
		return !result.empty();
	};
	get_value_text_async(dev_id, param, value, fn);
	auto lock = std::unique_lock{mutex};
	if (!cv.wait_for(lock, MAX_WAIT, ready)) {
		report::send(report::msg::error{"Timed out waiting for value text."});
	}
	return result;
}

static
auto get_value_text_async(id::device dev_id, idx::param param, double value, return_string fn) -> void {
	const auto m     = DATA_->model.read();
	const auto& dev  = m.devices.at(dev_id);
	const auto& sbox = m.sandboxes.at(dev.sbox);
	const auto callback = sbox.services->return_buffers.strings.put(fn);
	sbox.services->enqueue(scuff::msg::in::get_param_value_text{dev_id.value, param.value, value, callback});
}

[[nodiscard]] static
auto get_vendor(id::plugin plugin) -> const char* {
	return DATA_->model.read().plugins.at({plugin}).vendor->c_str();
}

[[nodiscard]] static
auto get_version(id::plugin plugin) -> const char* {
	return DATA_->model.read().plugins.at({plugin}).version->c_str();
}

static
auto restart(id::sandbox sbox, std::string_view sbox_exe_path) -> void {
	const auto m       = DATA_->model.read();
	const auto sandbox = m.sandboxes.at({sbox});
	const auto& group  = m.groups.at(sandbox.group);
	if (sandbox.services->proc.running()) {
		sandbox.services->proc.terminate();
	}
	const auto group_shmid   = group.services->shm.id();
	const auto sandbox_shmid = sandbox.services->get_shmid();
	const auto exe_args      = make_sbox_exe_args(group_shmid, sandbox_shmid);
	sandbox.services->proc   = bp::child{std::string{sbox_exe_path}, exe_args};
	// TODO: the rest of this
}

static
auto save_async(id::device dev, return_bytes fn) -> void {
	// TODO: impl::save_async
}

static
auto do_scan(std::string_view scan_exe_path, int flags) -> void {
	scan_::stop_if_it_is_already_running();
	scan_::start(scan_exe_path, flags);
}

[[nodiscard]] static
auto create_sandbox(id::group group_id, std::string_view sbox_exe_path) -> id::sandbox {
	const auto sbox_id = id::sandbox{id_gen_++};
	DATA_->model.update_publish([=](model&& m){
		sandbox sbox;
		sbox.id = id::sandbox{id_gen_++};
		try {
			const auto& group        = m.groups.at({group_id});
			const auto group_shmid   = group.services->shm.id();
			const auto sandbox_shmid = shm::sandbox::make_id(DATA_->instance_id, sbox.id);
			const auto exe_args      = make_sbox_exe_args(group_shmid, sandbox_shmid);
			auto proc                = bp::child{std::string{sbox_exe_path}, exe_args};
			sbox.group               = {group_id};
			sbox.services            = std::make_shared<sandbox_services>(std::move(proc), sandbox_shmid);
			// Add sandbox to group
			m = add_sandbox_to_group(m, {group_id}, sbox.id);
		}
		catch (const std::exception& err) {
			sbox.error = err.what();
			report::send(report::msg::error{err.what()});
			return m;
		}
		m.sandboxes = m.sandboxes.insert(sbox);
		return m;
	});
	if (DATA_->model.read().sandboxes.count(sbox_id) == 0) {
		// Failed to create the sandbox.
		return {};
	}
	return sbox_id;
}

static
auto erase(id::sandbox sbox) -> void {
	DATA_->model.update_publish([=](model&& m){
		const auto& sandbox = m.sandboxes.at({sbox});
		m = remove_sandbox_from_group(std::move(m), sandbox.group, {sbox});
		m.sandboxes = m.sandboxes.erase({sbox});
		return m;
	});
}

[[nodiscard]] static
auto get_error(id::sandbox sbox) -> const char* {
	return DATA_->model.read().sandboxes.at(sbox).error->c_str();
}

[[nodiscard]] static
auto get_working_plugins() -> std::vector<id::plugin> {
	std::vector<id::plugin> out;
	const auto m = DATA_->model.read();
	for (const auto& plugin : m.plugins) {
		if (plugin.error->empty()) {
			out.push_back(plugin.id);
		}
	}
	return out;
}

static
// TODO: this should be per-group
auto set_sample_rate(double sr) -> void {
	const auto m = DATA_->model.read();
	for (const auto& sbox : m.sandboxes) {
		if (sbox.services->proc.running()) {
			sbox.services->enqueue(msg::in::set_sample_rate{sr});
		}
	}
}

auto managed(id::device id) -> managed_device {
	ref(id);
	return {id};
}

auto managed(id::group id) -> managed_group {
	ref(id);
	return {id};
}

auto managed(id::sandbox id) -> managed_sandbox {
	ref(id);
	return {id};
}

auto ref(id::device id) -> void {
	DATA_->model.read().devices.at(id).services->ref_count++;
}

auto ref(id::group id) -> void {
	DATA_->model.read().groups.at(id).services->ref_count++;
}

auto ref(id::sandbox id) -> void {
	DATA_->model.read().sandboxes.at(id).services->ref_count++;
}

auto unref(id::device id) -> void {
	if (--DATA_->model.read().devices.at(id).services->ref_count <= 0) {
		erase(id);
	}
}

auto unref(id::group id) -> void {
	if (--DATA_->model.read().groups.at(id).services->ref_count <= 0) {
		erase(id);
	}
}

auto unref(id::sandbox id) -> void {
	if (--DATA_->model.read().sandboxes.at(id).services->ref_count <= 0) {
		erase(id);
	}
}

} // impl

namespace scuff {

auto audio_process(const group_process& process) -> void {
	const auto audio     = scuff::DATA_->model.rt_read();
	const auto& group    = audio->groups.at({process.group});
	const auto epoch     = ++group.services->epoch;
	impl::write_entry_ports(*audio, process.input_devices);
	if (impl::do_sandbox_processing(group, epoch)) {
		impl::read_exit_ports(*audio, process.output_devices);
	}
	else {
		impl::read_zeros(*audio, process.output_devices);
	}
}

auto init(const scuff::on_error& on_error) -> bool {
	if (scuff::initialized_) { return true; }
	try {
		scuff::DATA_              = std::make_unique<scuff::data>();
		scuff::DATA_->instance_id = "scuff+" + std::to_string(scuff::os::get_process_id());
		scuff::DATA_->poll_thread = std::jthread{impl::poll_thread};
		scuff::initialized_       = true;
		return true;
	} catch (const std::exception& err) {
		scuff::DATA_.reset();
		on_error(err.what());
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

auto activate(id::group group, double sr) -> void {
	try                               { impl::activate(group, sr); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto close_all_editors() -> void {
	try                               { impl::close_all_editors(); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto connect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void {
	try                               { impl::connect(dev_out, port_out, dev_in, port_in); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto create_device(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id) -> id::device {
	try                               { return impl::create_device(sbox, type, plugin_id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto create_device_async(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id, return_device fn) -> id::device {
	try                               { return impl::create_device_async(sbox, type, plugin_id, fn); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto create_sandbox(id::group group_id, std::string_view sbox_exe_path) -> id::sandbox {
	try                               { return impl::create_sandbox(group_id, sbox_exe_path); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto deactivate(id::group group) -> void {
	try                               { impl::deactivate(group); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto disconnect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void {
	try                               { impl::device_disconnect(dev_out, port_out, dev_in, port_in); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto duplicate(id::device dev, id::sandbox sbox) -> id::device {
	try                               { return impl::duplicate(dev, sbox); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto duplicate_async(id::device dev, id::sandbox sbox, return_device fn) -> id::device {
	try                               { return impl::duplicate_async(dev, sbox, fn); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto erase(id::device dev) -> void {
	try                               { impl::erase({dev}); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto erase(id::sandbox sbox) -> void {
	try                               { impl::erase(sbox); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto find(id::device dev, ext::id::param param_id) -> idx::param {
	try                               { return impl::find(dev, param_id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto find(ext::id::plugin plugin_id) -> id::plugin {
	try                               { return impl::find(plugin_id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto get_error(id::device device) -> const char* {
	try                               { return impl::get_error(device); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_name(id::device dev) -> const char* {
	try                               { return impl::get_name(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_param_count(id::device dev) -> size_t {
	try                               { return impl::get_param_count(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return 0; }
}

auto get_param_value_text(id::device dev, idx::param param, double value, return_string fn) -> void {
	try                               { impl::get_param_value_text(dev, param, value, fn); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto get_plugin(id::device dev) -> id::plugin {
	try                               { return impl::get_plugin(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto gui_hide(id::device dev) -> void {
	try                               { impl::gui_hide(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto gui_show(id::device dev) -> void {
	try                               { impl::gui_show(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto create_group(int flags) -> id::group {
	try                               { return impl::create_group(flags); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto erase(id::group group) -> void {
	try                               { impl::erase({group}); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto get_broken_plugfiles() -> std::vector<id::plugfile> {
	try                               { return impl::get_broken_plugfiles(); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto get_broken_plugins() -> std::vector<id::plugin> {
	try                               { return impl::get_broken_plugins(); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto get_error(id::plugfile plugfile) -> const char* {
	try                               { return impl::get_error(plugfile); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_error(id::plugin plugin) -> const char* {
	try                               { return impl::get_error(plugin); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_error(id::sandbox sbox) -> const char* {
	try                               { return impl::get_error(sbox); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_ext_id(id::plugin plugin) -> ext::id::plugin {
	try                               { return impl::get_ext_id(plugin); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto get_features(id::plugin plugin) -> std::vector<std::string> {
	try                               { return impl::get_features(plugin); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto get_info(id::device dev, idx::param param) -> param_info {
	try                               { return impl::get_info({dev}, param); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {0}; }
}

auto get_metadata(id::device dev, size_t column) -> std::any {
	try                               { return impl::get_metadata(dev, column); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto get_name(id::plugin plugin) -> const char* {
	try                               { return impl::get_name(plugin); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_path(id::plugfile plugfile) -> const char* {
	try                               { return impl::get_path(plugfile); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_plugin_ext_id(id::device dev) -> ext::id::plugin {
	try                               { return impl::get_plugin_ext_id(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto get_type(id::plugin plugin) -> plugin_type {
	try                               { return impl::get_type(plugin); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return plugin_type{}; }
}

auto get_value(id::device dev, idx::param param) -> double {
	try                               { return impl::get_value(dev, param); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto get_value_async(id::device dev, idx::param param, return_double fn) -> void {
	try                               { impl::get_value_async(dev, param, fn); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto get_value_text(id::device dev, idx::param param, double value) -> std::string {
	try                               { return impl::get_value_text(dev, param, value); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_value_text_async(id::device dev, idx::param param, double value, return_string fn) -> void {
	try                               { impl::get_value_text_async(dev, param, value, fn); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto get_vendor(id::plugin plugin) -> const char* {
	try                               { return impl::get_vendor(plugin); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_version(id::plugin plugin) -> const char* {
	try                               { return impl::get_version(plugin); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return ""; }
}

auto get_working_plugins() -> std::vector<id::plugin> {
	try                               { return impl::get_working_plugins(); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto has_gui(id::device dev) -> bool {
	try                               { return impl::has_gui(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return false; }
}

auto has_params(id::device dev) -> bool {
	try                               { return impl::has_params(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return false; }
}

auto has_rack_features(id::plugin plugin) -> bool {
	try                               { return impl::has_rack_features(plugin); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return false; }
}

auto is_running(id::sandbox sbox) -> bool {
	try                               { return impl::is_running(sbox); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return false; }
}

auto is_scanning() -> bool {
	try                               { return impl::is_scanning(); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return false; }
}

auto push_event(id::device dev, const scuff::event& event) -> void {
	try                               { impl::push_event(dev, event); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto restart(id::sandbox sbox, std::string_view sbox_exe_path) -> void {
	try                               { impl::restart(sbox, sbox_exe_path); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto scan(std::string_view scan_exe_path, int flags) -> void {
	try                               { impl::do_scan(scan_exe_path, flags); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto set_metadata(id::device dev, size_t column, std::any data) -> void {
	try                               { impl::set_metadata(dev, column, data); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto receive_report(const general_reporter& reporter) -> void {
	try                               { impl::receive_report(reporter); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto receive_report(id::group group_id, const group_reporter& reporter) -> void {
	try                               { impl::receive_report(group_id, reporter); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto save_async(id::device dev, return_bytes fn) -> void {
	try                               { impl::save_async(dev, fn); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto set_render_mode(id::device dev, render_mode mode) -> void {
	try                               { impl::set_render_mode(dev, mode); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto set_sample_rate(double sr) -> void {
	try                               { impl::set_sample_rate(sr); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto was_loaded_successfully(id::device dev) -> bool {
	try                               { return impl::was_loaded_successfully(dev); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return false; }
}

auto managed(id::device id) -> managed_device {
	try                               { return impl::managed(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto managed(id::group id) -> managed_group {
	try                               { return impl::managed(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto managed(id::sandbox id) -> managed_sandbox {
	try                               { return impl::managed(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); return {}; }
}

auto ref(id::device id) -> void {
	try                               { impl::ref(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto ref(id::group id) -> void {
	try                               { impl::ref(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto ref(id::sandbox id) -> void {
	try                               { impl::ref(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto unref(id::device id) -> void {
	try                               { impl::unref(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto unref(id::group id) -> void {
	try                               { impl::unref(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

auto unref(id::sandbox id) -> void {
	try                               { impl::unref(id); }
	catch (const std::exception& err) { report::send(report::msg::error{err.what()}); }
}

} // scuff
