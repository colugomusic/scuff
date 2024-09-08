#include "client.h"
#include "common/audio_sync.hpp"
#include "common/os.hpp"
#include "common/shm.hpp"
#include "common/slot_buffer.hpp"
#include "common/speen.hpp"
#include "common/types.hpp"
#include "common/visit.hpp"
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <clog/vectors.hpp>
#include <cs_plain_guarded.h>
#include <immer/box.hpp>
#include <immer/set.hpp>
#include <immer/table.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <readerwriterqueue.h>
#include <string>
#include <variant>

namespace basio = boost::asio;
namespace bip   = boost::interprocess;
namespace bp    = boost::process;
namespace bsys  = boost::system;

namespace scuff {

// TODO: exception handling around every entry point

using return_device_fns = slot_buffer<scuff_return_device>;
using return_double_fns = slot_buffer<scuff_return_double>;
using return_param_fns  = slot_buffer<scuff_return_param>;
using return_string_fns = slot_buffer<scuff_return_string>;

struct return_buffers {
	return_device_fns devices;
	return_double_fns doubles;
	return_param_fns  params;
	return_string_fns strings;
};

struct device_flags {
	enum e {
		has_gui          = 1 << 0,
		has_params       = 1 << 1,
		supports_offline = 1 << 2, // TODO: initialize these flags when device is created
	};
	int value = 0;
};

struct sbox_flags {
	enum e {
		running = 1 << 0,
	};
	int value = 0;
};

struct device_external {
	using shptr = std::shared_ptr<device_external>;
	shm::device_audio_ports shm_audio_ports;
	shm::device shm_device;
	shm::segment_remover shm_audio_ports_remover;
	shm::segment_remover shm_device_remover;
};

struct sandbox_external {
	using shptr = std::shared_ptr<sandbox_external>;
	shm::sandbox shm;
	shm::segment_remover shm_remover;
	std::unique_ptr<bp::child> proc;
	return_buffers return_buffers;
};

struct group_external {
	using shptr = std::shared_ptr<group_external>;
	shm::group shm;
	shm::segment_remover shm_remover;
	uint64_t epoch = 0;
};

struct device {
	id::device id;
	device_flags flags;
	id::plugin plugin;
	id::sandbox sbox;
	immer::box<std::string> error;
	immer::box<std::string> name;
	device_external::shptr external;
};

struct sandbox {
	id::sandbox id;
	id::group group;
	immer::box<std::string> error;
	sbox_flags flags;
	sandbox_external::shptr external;
};

struct group {
	id::group id;
	immer::set<id::sandbox> sandboxes;
	group_external::shptr external;
};

struct plugin {
	id::plugin id;
	ext::id::plugin ext_id;
	immer::box<std::string> error;
	immer::box<std::string> name;
	immer::box<std::string> vendor;
	immer::box<std::string> version;
};

struct plugfile {
	id::plugfile id;
	immer::box<std::string> error;
	immer::box<std::string> path;
};

struct model {
	immer::table<device> devices;
	immer::table<group> groups;
	immer::table<plugfile> plugfiles;
	immer::table<plugin> plugins;
	immer::table<sandbox> sandboxes;
};

struct scanner_proc {
	static constexpr auto STDOUT = 0;
	static constexpr auto STDERR = 1;
	basio::io_context* ioctx;
	std::array<basio::streambuf, 2> buffers;
	std::array<bp::async_pipe, 2> pipes;
	bp::child child;
	scanner_proc(basio::io_context* ioctx, const std::string& exe, const std::vector<std::string>& args)
		: ioctx{ioctx}
		, pipes{*ioctx, *ioctx}
		, child{os::start_child_process(exe, args, bp::std_out > pipes[STDOUT], bp::std_err > pipes[STDERR], *ioctx)}
	{
	}
};

struct scanner_reader {
	std::shared_ptr<scanner_proc> proc;
	basio::streambuf* buffer;
	bp::async_pipe* pipe;
	scanner_reader(std::shared_ptr<scanner_proc> proc, size_t stream)
		: proc{proc}
		, buffer{&proc->buffers[stream]}
		, pipe{&proc->pipes[stream]}
	{
	}
};

// DATA /////////////////////////////////////////////////////////////////////
struct data {
	shm::string_buffer                    shm_strings;
	shm::segment_remover                  shm_strings_remover;
	std::string                           instance_id;
	std::string                           sandbox_exe_path;
	std::string                           scanner_exe_path;
	scuff_callbacks                       callbacks;
	std::jthread                          gc_thread;
	std::jthread                          scanner_thread;

	// Copy of the model shared by non-audio threads. If a thread modifies
	// the model in a way that affects the audio thread then it should publish
	// the changes by calling publish().
	lg::plain_guarded<model>              working_model;

	// Copy of the model seen by the audio thread.
	audio_data<model>                     published_model;
};

static std::atomic_bool      initialized_ = false;
static std::atomic_int       id_gen_      = 0;
static std::unique_ptr<data> DATA_;
/////////////////////////////////////////////////////////////////////////////

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

[[nodiscard]] static
auto add_sandbox(model&& m, id::group group, id::sandbox sbox) -> model {
	m.groups = m.groups.update_if_exists(group, [sbox](scuff::group g) {
		g.sandboxes = g.sandboxes.insert(sbox);
		return g;
	});
	return m;
}

[[nodiscard]] static
auto remove_sandbox(model&& m, id::group group, id::sandbox sbox) -> model {
	m.groups = m.groups.update_if_exists(group, [sbox](scuff::group g) {
		g.sandboxes = g.sandboxes.erase(sbox);
		return g;
	});
	return m;
}

static
auto send(const sandbox& sbox, const scuff::msg::in::msg& msg) -> void {
	const auto lock = std::unique_lock(sbox.external->shm.msgs_in->mutex);
	// TODO: push onto local queue instead.
	assert (sbox.external->shm.msgs_in->list.size() < sbox.external->shm.msgs_in->list.max_size() && "send(sbox, in): Message buffer is full");
	sbox.external->shm.msgs_in->list.push_back(msg);
}

[[nodiscard]] static
auto make_sandbox_shm(std::string_view shmid) -> shm::sandbox {
	auto shm = shm::sandbox{};
	scuff::shm::create(&shm, shmid);
	return shm;
}

[[nodiscard]] static
auto make_sbox_exe_args(std::string_view group_id, std::string_view sandbox_id) -> std::vector<std::string> {
	std::vector<std::string> args;
	args.push_back(std::string("--group ") + group_id.data());
	args.push_back(std::string("--sandbox ") + sandbox_id.data());
	return args;
}

[[nodiscard]] static
auto make_scanner_exe_args_for_plugin_listing() -> std::vector<std::string> {
	return {};
}

[[nodiscard]] static
auto make_scanner_exe_args_for_scanning_plugin(const std::string& path) -> std::vector<std::string> {
	std::vector<std::string> args;
	args.push_back("--file");
	args.push_back(path);
	return args;
}

template <typename ReadFn> static
auto async_read_lines(const std::shared_ptr<scanner_reader>& reader, ReadFn&& read_fn) -> void {
	auto wrapper_fn = [reader, read_fn = std::forward<ReadFn>(read_fn)](const bsys::error_code& ec, size_t bytes_transferred) {
		read_fn(reader, ec, bytes_transferred);
	};
	basio::async_read_until(*reader->pipe, *reader->buffer, '\n', wrapper_fn);
}

static
auto scanner_read_broken_plugfile(const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	const std::string error         = j["error"];
	scuff::plugfile pf;
	pf.id   = id::plugfile{id_gen_++};
	pf.path = path;
	const auto model = DATA_->working_model.lock();
	model->plugfiles = model->plugfiles.insert(pf);
	DATA_->callbacks.on_plugfile_broken.fn(&DATA_->callbacks.on_plugfile_broken, pf.id.value);
}

static
auto scanner_read_broken_plugin(const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	const std::string error         = j["error"];
	if (plugfile_type == "clap") {
		const std::string name    = j["name"];
		const std::string id      = j["id"];
		const std::string url     = j["url"];
		const std::string vendor  = j["vendor"];
		const std::string version = j["version"];
		scuff::plugin plugin;
		plugin.id      = id::plugin{id_gen_++};
		plugin.ext_id  = ext::id::plugin{id};
		plugin.name    = name;
		plugin.vendor  = vendor;
		plugin.version = version;
		const auto model = DATA_->working_model.lock();
		model->plugins = model->plugins.insert(plugin);
		DATA_->callbacks.on_plugin_broken.fn(&DATA_->callbacks.on_plugin_broken, plugin.id.value);
	}
}

static auto scanner_read_stderr_line(const std::shared_ptr<scanner_reader>& reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;
static auto scanner_read_stdout_line(const std::shared_ptr<scanner_reader>& reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;

static
auto async_scan_clap_file(basio::io_context* ioctx, const std::string& path) -> void {
	const auto exe      = DATA_->scanner_exe_path;
	const auto exe_args = make_scanner_exe_args_for_scanning_plugin(path);
	auto proc           = std::make_shared<scanner_proc>(ioctx, exe, exe_args);
	auto stderr_reader  = std::make_shared<scanner_reader>(proc, scanner_proc::STDERR);
	auto stdout_reader  = std::make_shared<scanner_reader>(proc, scanner_proc::STDOUT);
	async_read_lines(stderr_reader, scanner_read_stderr_line);
	async_read_lines(stdout_reader, scanner_read_stdout_line);
}

static
auto scanner_read_plugfile(basio::io_context* ioctx, const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	plugfile pf;
	pf.id   = id::plugfile{id_gen_++};
	pf.path = path;
	const auto model = DATA_->working_model.lock();
	model->plugfiles = model->plugfiles.insert(pf);
	DATA_->callbacks.on_plugfile_scanned.fn(&DATA_->callbacks.on_plugfile_scanned, pf.id.value);
	async_scan_clap_file(ioctx, path);
}

static
auto scanner_read_plugin(const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	if (plugfile_type == "clap") {
		const std::string name    = j["name"];
		const std::string id      = j["id"];
		const std::string url     = j["url"];
		const std::string vendor  = j["vendor"];
		const std::string version = j["version"];
		scuff::plugin plugin;
		plugin.id      = id::plugin{id_gen_++};
		plugin.ext_id  = ext::id::plugin{id};
		plugin.name    = name;
		plugin.vendor  = vendor;
		plugin.version = version;
		const auto model = DATA_->working_model.lock();
		model->plugins = model->plugins.insert(plugin);
		DATA_->callbacks.on_plugin_scanned.fn(&DATA_->callbacks.on_plugin_scanned, plugin.id.value);
	}
}

static
auto scanner_read_error(const nlohmann::json& j) -> void {
	const std::string type = j["type"];
	if (type == "broken-plugfile") { scanner_read_broken_plugfile(j); return; }
	if (type == "broken-plugin")   { scanner_read_broken_plugin(j); return; }
}

static
auto scanner_read_output(basio::io_context* ioctx, const nlohmann::json& j) -> void {
	const std::string type = j["type"];
	if (type == "plugfile") { scanner_read_plugfile(ioctx, j); return; }
	if (type == "plugin")   { scanner_read_plugin(j); return; }
}

static
auto report_scan_error(std::string_view err) -> void {
	DATA_->callbacks.on_scan_error.fn(&DATA_->callbacks.on_scan_error, err.data());
}

static
auto report_scan_exception(const std::exception& err) -> void {
	report_scan_error(err.what());
}

static
auto scanner_read_stderr_line(const std::shared_ptr<scanner_reader>& reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	if (ec) {
		return;
	}
	auto is   = std::istream{reader->buffer};
	auto line = std::string{};
	std::getline(is, line);
	if (!line.empty()) {
		try { scanner_read_error(nlohmann::json::parse(line)); }
		catch (const std::exception& err) {
			report_scan_exception(err);
		}
	}
	// continue reading
	async_read_lines(reader, scanner_read_stderr_line);
}

static
auto scanner_read_stdout_line(const std::shared_ptr<scanner_reader>& reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	if (ec) {
		return;
	}
	auto is   = std::istream{reader->buffer};
	auto line = std::string{};
	std::getline(is, line);
	if (!line.empty()) {
		try { scanner_read_output(reader->proc->ioctx, nlohmann::json::parse(line)); }
		catch (const std::exception& err) {
			report_scan_exception(err);
		}
	}
	// continue reading
	async_read_lines(reader, scanner_read_stdout_line);
}

static
auto scan_system_for_installed_plugins(basio::io_context* ioctx) -> void {
	const auto exe           = DATA_->scanner_exe_path;
	if (!(std::filesystem::exists(exe) && std::filesystem::is_regular_file(exe))) {
		const auto err = std::format("Scanner executable not found: {}", exe);
		report_scan_error(err);
		return;
	}
	const auto exe_args      = make_scanner_exe_args_for_plugin_listing();
	const auto proc          = std::make_shared<scanner_proc>(ioctx, exe, exe_args);
	const auto stderr_reader = std::make_shared<scanner_reader>(proc, scanner_proc::STDERR);
	const auto stdout_reader = std::make_shared<scanner_reader>(proc, scanner_proc::STDOUT);
	async_read_lines(stderr_reader, scanner_read_stderr_line);
	async_read_lines(stdout_reader, scanner_read_stdout_line);
}

static
auto scanner_thread() -> void {
	basio::io_context ioctx;
	basio::post(ioctx, [&ioctx]{ scan_system_for_installed_plugins(&ioctx); });
	ioctx.run();
	DATA_->callbacks.on_scan_complete.fn(&DATA_->callbacks.on_scan_complete);
}

static
auto scanner_start() -> void {
	DATA_->scanner_thread = std::jthread{scanner_thread};
}

static
auto scanner_stop_if_it_is_already_running() -> void {
	if (DATA_->scanner_thread.joinable()) {
		DATA_->scanner_thread.request_stop();
		DATA_->scanner_thread.join();
	}
}

[[nodiscard]] static
auto wait_for_output_ready(const scuff::group& group, size_t frontside) -> bool {
	auto ready = [cb = group.external->shm.cb, frontside]() -> bool {
		return cb->sandboxes_processing.value[frontside].load(std::memory_order_acquire) < 1;
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
	auto& ab               = *dev.external->shm_device.events_in;
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
	std::array<float, TOM_CHANNEL_COUNT * TOM_VECTOR_SIZE> zeros = {0.0f};
	for (size_t j = 0; j < readers.count; j++) {
		const auto& reader = readers.readers[j];
		reader.fn(&reader, zeros.data());
	}
}

static
auto read_events(const scuff::device& dev, const scuff_event_reader& reader, size_t frontside) -> void {
	auto& ab               = *dev.external->shm_device.events_out;
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
	auto& cb = *group.external->shm.cb;
	// Set the sandbox counter
	cb.sandboxes_processing.value[backside].store(group.sandboxes.size(), std::memory_order_seq_cst);
	// Set the epoch. This will trigger the sandboxes to begin processing.
	cb.epoch.store(epoch, std::memory_order_release);
}

static
auto garbage_collector(std::stop_token stop_token, size_t interval_ms) -> void {
	while (!stop_token.stop_requested()) {
		DATA_->published_model.garbage_collect();
		std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
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

auto scuff_init(const scuff_config* config) -> void {
	if (scuff::initialized_) { return; }
	scuff::DATA_                      = std::make_unique<scuff::data>();
	scuff::DATA_->callbacks           = config->callbacks;
	scuff::DATA_->instance_id         = "scuff+" + std::to_string(scuff::os::get_process_id());
	scuff::DATA_->sandbox_exe_path    = config->sandbox_exe_path;
	scuff::DATA_->scanner_exe_path    = config->scanner_exe_path;
	scuff::DATA_->gc_thread           = std::jthread{scuff::garbage_collector, config->gc_interval_ms};
	scuff::shm::create(&scuff::DATA_->shm_strings, scuff::DATA_->instance_id + "+strings", config->string_options);
	scuff::DATA_->shm_strings_remover = {scuff::DATA_->shm_strings.id};
	scuff::initialized_ = true;
}

auto scuff_shutdown() -> void {
	if (!scuff::initialized_) { return; }
	scuff::DATA_->gc_thread.request_stop();
	scuff::DATA_->gc_thread.detach();
	scuff::DATA_.reset();
	scuff::initialized_ = false;
}

auto scuff_close_all_editors() -> void {
	const auto model = scuff::DATA_->working_model.lock();
	for (const auto& sandbox : model->sandboxes) {
		if (sandbox.external->proc && sandbox.external->proc->running()) {
			send(sandbox, scuff::msg::in::close_all_editors{});
		}
	}
}

auto scuff_device_connect(scuff_device dev_out, size_t port_out, scuff_device dev_in, size_t port_in) -> void {
	// TODO:
}

auto scuff_device_create(scuff_sbox sbox, scuff_plugin plugin, scuff_return_device fn) -> void {
	// TODO:
	// - create device table entry, shared memory, etc.
	// - then send message to sandbox
	// - if result is success, call fn with the handle
	// - otherwise, erase device table entry, shared memory, etc.
	// - remember to create segment remover
}

auto scuff_device_disconnect(scuff_device dev_out, size_t port_out, scuff_device dev_in, size_t port_in) -> void {
	// TODO:
}

auto scuff_device_duplicate(scuff_device dev, scuff_sbox sbox, scuff_return_device fn) -> void {
	// TODO: similar to create()?
}

auto scuff_device_get_error(scuff_device device) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->devices.at({device}).error->c_str();
}

auto scuff_device_get_name(scuff_device dev) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->devices.at({dev}).name->c_str();
}

auto scuff_device_get_param_count(scuff_device dev) -> size_t {
	const auto m = scuff::DATA_->working_model.lock();
	// TODO:
	return {};
}

auto scuff_device_get_param_value_text(scuff_device dev, scuff_param param, double value, scuff_return_string fn) -> void {
	const auto& m       = scuff::DATA_->working_model.lock();
	const auto& device  = m->devices.at({dev});
	const auto& sbox    = m->sandboxes.at(device.sbox);
	const auto callback = sbox.external->return_buffers.strings.put(fn);
	send(sbox, scuff::msg::in::get_param_value_text{dev, param, value, callback});
}

auto scuff_device_get_plugin(scuff_device dev) -> scuff_plugin {
	const auto m = scuff::DATA_->working_model.lock();
	return m->devices.at({dev}).plugin.value;
}

auto scuff_device_has_gui(scuff_device dev) -> bool {
	const auto m = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	return device.flags.value & device.flags.has_gui;
}

auto scuff_device_has_params(scuff_device dev) -> bool {
	const auto m = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	return device.flags.value & device.flags.has_params;
}

auto scuff_device_set_render_mode(scuff_device dev, scuff_render_mode mode) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::device_set_render_mode{dev, mode});
}

auto scuff_device_gui_hide(scuff_device dev) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::device_gui_hide{dev});
}

auto scuff_device_gui_show(scuff_device dev) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::device_gui_show{dev});
}

auto scuff_device_was_loaded_successfully(scuff_device dev) -> bool {
	const auto m = scuff::DATA_->working_model.lock();
	return m->devices.at({dev}).error->empty();
}

auto scuff_group_create() -> scuff_group {
	// TODO:
	// - remember to create segment remover
	return scuff::id::group{}.value;
}

auto scuff_is_running(scuff_sbox sbox) -> bool {
	const auto m        = scuff::DATA_->working_model.lock();
	const auto& sandbox = m->sandboxes.at({sbox});
	return sandbox.external->proc && sandbox.external->proc->running();
}

auto scuff_is_scanning() -> bool {
	return scuff::DATA_->scanner_thread.joinable();
}

auto scuff_param_get_value(scuff_device dev, scuff_param param, scuff_return_double fn) -> void {
	const auto& m       = scuff::DATA_->working_model.lock();
	const auto& device  = m->devices.at({dev});
	const auto& sbox    = m->sandboxes.at(device.sbox);
	const auto callback = sbox.external->return_buffers.doubles.put(fn);
	send(sbox, scuff::msg::in::get_param_value{dev, param, callback});
}

auto scuff_param_find(scuff_device dev, scuff_param_id param_id, scuff_return_param fn) -> void {
	const auto& model       = scuff::DATA_->working_model.lock();
	const auto& device      = model->devices.at({dev});
	const auto& sbox        = model->sandboxes.at(device.sbox);
	const auto callback     = sbox.external->return_buffers.params.put(fn);
	const auto STR_param_id = scuff::shm::put(&scuff::DATA_->shm_strings, {param_id});
	send(sbox, scuff::msg::in::find_param{dev, STR_param_id, callback});
}

auto scuff_param_gesture_begin(scuff_device dev, scuff_param param) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::event{dev, scuff_event_param_gesture_begin{/*TODO:*/}});
}

auto scuff_param_gesture_end(scuff_device dev, scuff_param param) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::event{dev, scuff_event_param_gesture_end{/*TODO:*/}});
}

auto scuff_param_set_value(scuff_device dev, scuff_param param, double value) -> void {
	const auto m       = scuff::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, scuff::msg::in::event{dev, scuff_event_param_value{/*TODO:*/}});
}

auto scuff_plugin_find(scuff_plugin_id plugin_id) -> scuff_plugin {
	const auto& m = scuff::DATA_->working_model.lock();
	for (const auto& plugin : m->plugins) {
		if (plugin.ext_id.value == plugin_id) {
			return plugin.id.value;
		}
	}
	return scuff::id::plugin{}.value;
}

auto scuff_plugfile_get_path(scuff_plugfile plugfile) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugfiles.at({plugfile}).path->c_str();
}

auto scuff_plugfile_get_error(scuff_plugfile plugfile) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugfiles.at({plugfile}).error->c_str();
}

auto scuff_plugin_get_error(scuff_plugin plugin) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).error->c_str();
}

auto scuff_plugin_get_id(scuff_plugin plugin) -> scuff_plugin_id {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).ext_id.value.c_str();
}

auto scuff_plugin_get_name(scuff_plugin plugin) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).name->c_str();
}

auto scuff_plugin_get_vendor(scuff_plugin plugin) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).vendor->c_str();
}

auto scuff_plugin_get_version(scuff_plugin plugin) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->plugins.at({plugin}).version->c_str();
}

auto scuff_restart(scuff_sbox sbox) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	const auto sandbox = m->sandboxes.at({sbox});
	if (sandbox.external->proc && sandbox.external->proc->running()) {
		sandbox.external->proc->terminate();
	}
	// TODO:
}

auto scuff_scan() -> void {
	try {
		scuff::scanner_stop_if_it_is_already_running();
		scuff::scanner_start();
	}
	catch (const std::exception& err) {
		scuff::report_scan_error(err.what());
	}
}

auto scuff_sandbox_create(scuff_group group_id) -> scuff_sbox {
	const auto m = scuff::DATA_->working_model.lock();
	scuff::sandbox sbox;
	sbox.id = scuff::id::sandbox{scuff::id_gen_++};
	try {
		const auto& group          = m->groups.at({group_id});
		const auto group_shmid     = group.external->shm.id;
		const auto sandbox_shmid   = scuff::DATA_->instance_id + "+sbox+" + std::to_string(sbox.id.value);
		const auto exe_args        = scuff::make_sbox_exe_args(group_shmid, sandbox_shmid);
		sbox.group                 = {group_id};
		sbox.external              = std::make_shared<scuff::sandbox_external>();
		sbox.external->shm         = scuff::make_sandbox_shm(sandbox_shmid);
		sbox.external->shm_remover = {sbox.external->shm.id};
		sbox.external->proc        = std::make_unique<bp::child>(scuff::DATA_->sandbox_exe_path, exe_args);
		// Add sandbox to group
		*m = add_sandbox(std::move(*m), {group_id}, sbox.id);
	}
	catch (const std::exception& err) {
		sbox.error = err.what();
		// TODO: invoke a callback or something?
	}
	m->sandboxes = m->sandboxes.insert(sbox);
	scuff::publish(*m);
	return sbox.id.value;
}

auto scuff_sandbox_erase(scuff_sbox sbox) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	const auto& sandbox = m->sandboxes.at({sbox});
	*m = remove_sandbox(std::move(*m), sandbox.group, {sbox});
	m->sandboxes = m->sandboxes.erase({sbox});
	scuff::publish(*m);
}

auto scuff_sandbox_get_error(scuff_sbox sbox) -> const char* {
	const auto m = scuff::DATA_->working_model.lock();
	return m->sandboxes.at({sbox}).error->c_str();
}

auto scuff_set_sample_rate(scuff_sample_rate sr) -> void {
	const auto m = scuff::DATA_->working_model.lock();
	for (const auto& sbox : m->sandboxes) {
		if (sbox.external->proc && sbox.external->proc->running()) {
			send(sbox, scuff::msg::in::set_sample_rate{sr});
		}
	}
}
