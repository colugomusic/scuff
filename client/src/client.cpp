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
#include <readerwriterqueue.h>
#include <string>
#include <variant>

namespace basio = boost::asio;
namespace bip   = boost::interprocess;
namespace bp    = boost::process;
namespace bsys  = boost::system;

namespace tom {

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

struct scanner_proc {
	using uptr = std::unique_ptr<scanner_proc>;
	basio::io_context io_context;
	basio::streambuf buffer;
	basio::steady_timer check_exit_timer{io_context, basio::chrono::seconds(1)};
	bp::async_pipe pipe{io_context};
	bp::child child;
};

struct model {
	immer::table<device> devices;
	immer::table<group> groups;
	immer::table<plugfile> plugfiles;
	immer::table<plugin> plugins;
	immer::table<sandbox> sandboxes;
};

// DATA /////////////////////////////////////////////////////////////////////
struct data {
	lg::plain_guarded<scanner_proc::uptr> scanner;
	shm::string_buffer                    shm_strings;
	shm::segment_remover                  shm_strings_remover;
	std::string                           instance_id;
	scuff_callbacks                         callbacks;

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
auto convert(const scuff_event_header& header) -> tom::events::event {
	switch (header.type) {
		case scuff_event_type_param_gesture_begin: { return convert<scuff_event_param_gesture_begin>(header); }
		case scuff_event_type_param_gesture_end:   { return convert<scuff_event_param_gesture_end>(header); }
		case scuff_event_type_param_value:         { return convert<scuff_event_param_value>(header); }
	}
	assert (false && "Invalid event header");
	return {};
}

[[nodiscard]] static
auto convert(const tom::events::event& event) -> const scuff_event_header& {
	return fast_visit([](const auto& event) -> const scuff_event_header& { return event.header; }, event);
}

[[nodiscard]] static
auto add_sandbox(model&& m, id::group group, id::sandbox sbox) -> model {
	m.groups = m.groups.update_if_exists(group, [sbox](tom::group g) {
		g.sandboxes = g.sandboxes.insert(sbox);
		return g;
	});
	return m;
}

[[nodiscard]] static
auto remove_sandbox(model&& m, id::group group, id::sandbox sbox) -> model {
	m.groups = m.groups.update_if_exists(group, [sbox](tom::group g) {
		g.sandboxes = g.sandboxes.erase(sbox);
		return g;
	});
	return m;
}

static
auto send(const sandbox& sbox, const tom::msg::in::msg& msg) -> void {
	const auto lock = std::unique_lock(sbox.external->shm.msgs_in->mutex);
	// TODO: push onto local queue instead.
	assert (sbox.external->shm.msgs_in->list.size() < sbox.external->shm.msgs_in->list.max_size() && "send(sbox, in): Message buffer is full");
	sbox.external->shm.msgs_in->list.push_back(msg);
}

[[nodiscard]] static
auto make_sandbox_shm(std::string_view shmid) -> shm::sandbox {
	auto shm = shm::sandbox{};
	tom::shm::create(&shm, shmid);
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
auto make_scanner_exe_args(std::string_view id) -> std::vector<std::string> {
	std::vector<std::string> args;
	args.push_back(std::string("--id ") + id.data());
	return args;
}

static
auto scanner_check_exit(scanner_proc* proc) -> void {
	if (!proc->child.running()) {
		// TODO: invoke a callback
	}
	// Check again later
	proc->check_exit_timer.expires_after(basio::chrono::seconds(1));
	proc->check_exit_timer.async_wait([proc](const bsys::error_code& ec) {
		if (!ec) {
			scanner_check_exit(proc);
		}
	});
}

static
auto scanner_read_line(scanner_proc* proc, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	// TODO: read JSON description of plugin
}

static
auto scanner_start() -> void {
	const auto id       = DATA_->instance_id + "+scanner+" + std::to_string(id_gen_++);
	const auto exe      = "tom-scanner.exe";
	const auto exe_args = make_scanner_exe_args(id);
	auto proc              = std::make_unique<scanner_proc>();
	proc->pipe             = bp::async_pipe(proc->io_context);
	proc->child            = bp::child(exe, exe_args, bp::std_out > proc->pipe, proc->io_context);
	proc->check_exit_timer = basio::steady_timer(proc->io_context, basio::chrono::seconds(1));
	basio::async_read_until(proc->pipe, proc->buffer, '\n',
		[proc = proc.get()](const bsys::error_code& ec, size_t bytes_transferred) {
			scanner_read_line(proc, ec, bytes_transferred);
		});
	scanner_check_exit(proc.get());
	proc->io_context.run();
	*DATA_->scanner.lock() = std::move(proc);
}

static
auto scanner_stop_if_it_is_already_running() -> void {
	auto scanner = DATA_->scanner.lock();
	if (!scanner) { return; }
	const auto& proc = *scanner;
	if (!proc) { return; }
	if (!proc->child.running()) { return; }
	proc->child.terminate();
	(*scanner).reset();
}

static
auto write_entry_ports(const tom::model& m, const tom::group& group, const scuff_audio_input_port& audio_in, size_t audio_in_count, const scuff_event_input_port& events_in, size_t events_in_count) -> void {
	const auto epoch = group.external->shm.cb->epoch.load(std::memory_order_acquire);
	// (epoch + 1) % 2 is going to be read by the sandbox
	// processes, so that's where data is written.
	const auto write_side = (epoch + 1) % 2;
	for (size_t i = 0; i < audio_in_count; i++) {
		const auto port = (&audio_in)[i];
		const auto& dev = m.devices.at({port.dev});
		auto floats = dev.external->shm_audio_ports.input_buffers[(port.port_index * 2) + write_side].data();
		port.writer.fn(&port.writer, floats);
	}
	for (size_t i = 0; i < events_in_count; i++) {
		const auto port = (&events_in)[i];
		const auto& dev = m.devices.at({port.dev});
		const auto event_count = port.writer.count(&port.writer);
		assert (event_count <= TOM_EVENT_PORT_SIZE);
		for (size_t j = 0; j < event_count; j++) {
			const auto event = port.writer.get(&port.writer, j);
			auto& buffer = dev.external->shm_device.events_in[write_side];
			buffer.push_back(convert(*event)); // TODO: make sure this is cleared when reading in sbox process
		}
	}
}

static
auto read_exit_ports(const tom::model& m, const tom::group& group, const scuff_audio_output_port& audio_out, size_t audio_out_count, const scuff_event_output_port& events_out, size_t events_out_count) -> void {
	const auto epoch = group.external->shm.cb->epoch.load(std::memory_order_acquire);
	// (epoch % 2) is where data was just written by the
	// sandbox processes, so that's where we read from.
	const auto read_side = (epoch % 2);
	for (size_t i = 0; i < audio_out_count; i++) {
		const auto port = (&audio_out)[i];
		const auto& dev = m.devices.at({port.dev});
		auto floats = dev.external->shm_audio_ports.output_buffers[(port.port_index * 2) + read_side].data();
		port.reader.fn(&port.reader, floats);
	}
	for (size_t i = 0; i < events_out_count; i++) {
		const auto port = (&events_out)[i];
		const auto& dev = m.devices.at({port.dev});
		for (const auto& event : dev.external->shm_device.events_out[read_side]) {
			const auto converted = convert(event);
			port.reader.push(&port.reader, &converted);
		}
	}
}

static
auto process_sandboxes(const tom::group& group) -> void {
	// Set the done counter to zero.
	group.external->shm.cb->sandboxes_done.store(0);
	// Increment the epoch. This triggers every
	// sandbox in the group to begin processing.
	const auto epoch = group.external->shm.cb->epoch.fetch_add(1) + 1;
	auto done = [&group]() -> bool {
		const auto sandboxes_done = group.external->shm.cb->sandboxes_done.load(std::memory_order_acquire);
		const auto sandbox_count  = group.sandboxes.size();
		return sandboxes_done >= sandbox_count;
	};
	if (done()) {
		return;
	}
	// Spin-wait until the sandboxes have finished processing.
	// If a sandbox is misbehaving then this might time out
	// and the buffer would be missed.
	auto success = speen::wait_for_a_bit(done);
	assert (success && "Sandbox timed out");
}

} // tom

auto scuff_audio_process(scuff_group_process process) -> void {
	const auto audio = tom::DATA_->published_model.read();
	const auto& group = audio->groups.at({process.group});
	tom::write_entry_ports(*audio, group, *process.input_audio_ports, process.audio_input_ports_count, *process.input_events, process.event_input_ports_count);
	tom::process_sandboxes(group);
	tom::read_exit_ports(*audio, group, *process.output_audio_ports, process.audio_output_ports_count, *process.output_events, process.event_output_ports_count);
}

auto scuff_init(const scuff_config* config) -> void {
	if (tom::initialized_) { return; }
	tom::DATA_ = std::make_unique<tom::data>();
	tom::DATA_->callbacks   = config->callbacks;
	tom::DATA_->instance_id = "tom+" + std::to_string(tom::os::get_process_id());
	tom::shm::create(&tom::DATA_->shm_strings, tom::DATA_->instance_id + "+string", config->string_options);
	tom::DATA_->shm_strings_remover = {tom::DATA_->shm_strings.id};
	tom::initialized_ = true;
}

auto scuff_shutdown() -> void {
	if (!tom::initialized_) { return; }
	tom::DATA_.reset();
	tom::initialized_ = false;
}

auto scuff_close_all_editors() -> void {
	const auto model = tom::DATA_->working_model.lock();
	for (const auto& sandbox : model->sandboxes) {
		if (sandbox.external->proc && sandbox.external->proc->running()) {
			send(sandbox, tom::msg::in::close_all_editors{});
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
	const auto m = tom::DATA_->working_model.lock();
	return m->devices.at({device}).error->c_str();
}

auto scuff_device_get_name(scuff_device dev) -> const char* {
	const auto m = tom::DATA_->working_model.lock();
	return m->devices.at({dev}).name->c_str();
}

auto scuff_device_get_param_count(scuff_device dev) -> size_t {
	const auto m = tom::DATA_->working_model.lock();
	// TODO:
	return {};
}

auto scuff_device_get_param_value_text(scuff_device dev, scuff_param param, double value, scuff_return_string fn) -> void {
	const auto& m       = tom::DATA_->working_model.lock();
	const auto& device  = m->devices.at({dev});
	const auto& sbox    = m->sandboxes.at(device.sbox);
	const auto callback = sbox.external->return_buffers.strings.put(fn);
	send(sbox, tom::msg::in::get_param_value_text{dev, param, value, callback});
}

auto scuff_device_get_plugin(scuff_device dev) -> scuff_plugin {
	const auto m = tom::DATA_->working_model.lock();
	return m->devices.at({dev}).plugin.value;
}

auto scuff_device_has_gui(scuff_device dev) -> bool {
	const auto m = tom::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	return device.flags.value & device.flags.has_gui;
}

auto scuff_device_has_params(scuff_device dev) -> bool {
	const auto m = tom::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	return device.flags.value & device.flags.has_params;
}

auto scuff_device_set_render_mode(scuff_device dev, scuff_render_mode mode) -> void {
	const auto m       = tom::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, tom::msg::in::device_set_render_mode{dev, tom::render_mode{mode}});
}

auto scuff_device_gui_hide(scuff_device dev) -> void {
	const auto m       = tom::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, tom::msg::in::device_gui_hide{dev});
}

auto scuff_device_gui_show(scuff_device dev) -> void {
	const auto m       = tom::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, tom::msg::in::device_gui_show{dev});
}

auto scuff_device_was_loaded_successfully(scuff_device dev) -> bool {
	const auto m = tom::DATA_->working_model.lock();
	return m->devices.at({dev}).error->empty();
}

auto scuff_group_create() -> scuff_group {
	// TODO:
	// - remember to create segment remover
	return tom::id::group{}.value;
}

auto scuff_is_running(scuff_sbox sbox) -> bool {
	const auto m        = tom::DATA_->working_model.lock();
	const auto& sandbox = m->sandboxes.at({sbox});
	return sandbox.external->proc && sandbox.external->proc->running();
}

auto scuff_is_scanning() -> bool {
	return bool(*tom::DATA_->scanner.lock());
}

auto scuff_param_get_value(scuff_device dev, scuff_param param, scuff_return_double fn) -> void {
	const auto& m       = tom::DATA_->working_model.lock();
	const auto& device  = m->devices.at({dev});
	const auto& sbox    = m->sandboxes.at(device.sbox);
	const auto callback = sbox.external->return_buffers.doubles.put(fn);
	send(sbox, tom::msg::in::get_param_value{dev, param, callback});
}

auto scuff_param_find(scuff_device dev, scuff_param_id param_id, scuff_return_param fn) -> void {
	const auto& model       = tom::DATA_->working_model.lock();
	const auto& device      = model->devices.at({dev});
	const auto& sbox        = model->sandboxes.at(device.sbox);
	const auto callback     = sbox.external->return_buffers.params.put(fn);
	const auto STR_param_id = tom::shm::put(&tom::DATA_->shm_strings, {param_id});
	send(sbox, tom::msg::in::find_param{dev, STR_param_id, callback});
}

auto scuff_param_gesture_begin(scuff_device dev, scuff_param param) -> void {
	const auto m = tom::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, tom::msg::in::event{dev, scuff_event_param_gesture_begin{/*TODO:*/}});
}

auto scuff_param_gesture_end(scuff_device dev, scuff_param param) -> void {
	const auto m = tom::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, tom::msg::in::event{dev, scuff_event_param_gesture_end{/*TODO:*/}});
}

auto scuff_param_set_value(scuff_device dev, scuff_param param, double value) -> void {
	const auto m       = tom::DATA_->working_model.lock();
	const auto& device = m->devices.at({dev});
	const auto& sbox   = m->sandboxes.at(device.sbox);
	send(sbox, tom::msg::in::event{dev, scuff_event_param_value{/*TODO:*/}});
}

auto scuff_plugin_find(scuff_plugin_id plugin_id) -> scuff_plugin {
	const auto& m = tom::DATA_->working_model.lock();
	for (const auto& plugin : m->plugins) {
		if (plugin.ext_id.value == plugin_id) {
			return plugin.id.value;
		}
	}
	return tom::id::plugin{}.value;
}

auto scuff_plugfile_get_path(scuff_plugfile plugfile) -> const char* {
	const auto m = tom::DATA_->working_model.lock();
	return m->plugfiles.at({plugfile}).path->c_str();
}

auto scuff_plugfile_get_error(scuff_plugfile plugfile) -> const char* {
	const auto m = tom::DATA_->working_model.lock();
	return m->plugfiles.at({plugfile}).error->c_str();
}

auto scuff_plugin_get_error(scuff_plugin plugin) -> const char* {
	const auto m = tom::DATA_->working_model.lock();
	return m->plugins.at({plugin}).error->c_str();
}

auto scuff_plugin_get_id(scuff_plugin plugin) -> scuff_plugin_id {
	const auto m = tom::DATA_->working_model.lock();
	return m->plugins.at({plugin}).ext_id.value.c_str();
}

auto scuff_plugin_get_name(scuff_plugin plugin) -> const char* {
	const auto m = tom::DATA_->working_model.lock();
	return m->plugins.at({plugin}).name->c_str();
}

auto scuff_plugin_get_vendor(scuff_plugin plugin) -> const char* {
	const auto m = tom::DATA_->working_model.lock();
	return m->plugins.at({plugin}).vendor->c_str();
}

auto scuff_plugin_get_version(scuff_plugin plugin) -> const char* {
	const auto m = tom::DATA_->working_model.lock();
	return m->plugins.at({plugin}).version->c_str();
}

auto scuff_restart(scuff_sbox sbox) -> void {
	const auto m = tom::DATA_->working_model.lock();
	const auto sandbox = m->sandboxes.at({sbox});
	if (sandbox.external->proc && sandbox.external->proc->running()) {
		sandbox.external->proc->terminate();
	}
	// TODO:
}

auto scuff_scan() -> void {
	try {
		tom::scanner_stop_if_it_is_already_running();
		tom::scanner_start();
	}
	catch (const std::exception& err) {
		// TODO: invoke a callback or something?
	}
}

auto scuff_sandbox_create(scuff_group group_id) -> scuff_sbox {
	const auto m = tom::DATA_->working_model.lock();
	tom::sandbox sbox;
	sbox.id = tom::id::sandbox{tom::id_gen_++};
	try {
		const auto& group          = m->groups.at({group_id});
		const auto group_shmid     = group.external->shm.id;
		const auto sandbox_shmid   = tom::DATA_->instance_id + "+sbox+" + std::to_string(sbox.id.value);
		const auto exe             = "tom-sbox.exe";
		const auto exe_args        = tom::make_sbox_exe_args(group_shmid, sandbox_shmid);
		sbox.group                 = {group_id};
		sbox.external              = std::make_shared<tom::sandbox_external>();
		sbox.external->shm         = tom::make_sandbox_shm(sandbox_shmid);
		sbox.external->shm_remover = {sbox.external->shm.id};
		// TODO: make sure shm processing flag is set back to false if the sandbox process crashes or exits.
		sbox.external->proc        = std::make_unique<bp::child>(exe, exe_args);
		// Add sandbox to group
		*m = add_sandbox(std::move(*m), {group_id}, sbox.id);
	}
	catch (const std::exception& err) {
		sbox.error = err.what();
		// TODO: invoke a callback or something?
	}
	m->sandboxes = m->sandboxes.insert(sbox);
	tom::publish(*m);
	return sbox.id.value;
}

auto scuff_sandbox_erase(scuff_sbox sbox) -> void {
	const auto m = tom::DATA_->working_model.lock();
	const auto& sandbox = m->sandboxes.at({sbox});
	*m = remove_sandbox(std::move(*m), sandbox.group, {sbox});
	m->sandboxes = m->sandboxes.erase({sbox});
	tom::publish(*m);
}

auto scuff_sandbox_get_error(scuff_sbox sbox) -> const char* {
	const auto m = tom::DATA_->working_model.lock();
	return m->sandboxes.at({sbox}).error->c_str();
}

auto scuff_set_sample_rate(scuff_sample_rate sr) -> void {
	const auto m = tom::DATA_->working_model.lock();
	for (const auto& sbox : m->sandboxes) {
		if (sbox.external->proc && sbox.external->proc->running()) {
			send(sbox, tom::msg::in::set_sample_rate{sr});
		}
	}
}
