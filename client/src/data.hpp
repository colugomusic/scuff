#pragma once

#include "client.h"
#include "common/audio_sync.hpp"
#include "common/shm.hpp"
#include "common/slot_buffer.hpp"
#include <boost/asio.hpp>
#include <cs_plain_guarded.h>
#include <deque>
#include <immer/box.hpp>
#include <immer/set.hpp>
#include <immer/table.hpp>

namespace basio = boost::asio;

namespace scuff {

using return_device = std::function<void(id::device dev_id, bool success)>;
using return_state  = std::function<void(const std::vector<std::byte>& bytes)>;

using return_device_fns = slot_buffer<return_device>;
using return_double_fns = slot_buffer<scuff_return_double>;
using return_param_fns  = slot_buffer<scuff_return_param>;
using return_state_fns  = slot_buffer<return_state>;
using return_string_fns = slot_buffer<scuff_return_string>;

struct return_buffers {
	return_device_fns devices;
	return_double_fns doubles;
	return_param_fns  params;
	return_state_fns states;
	return_string_fns strings;
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
};

struct sandbox_external {
	using shptr = std::shared_ptr<sandbox_external>;
	bp::child proc;
	return_buffers return_buffers;
	sandbox_external(bp::child&& proc, std::string_view shmid)
		: proc{std::move(proc)}
		, shm_{bip::create_only, shm::segment::remove_when_done, shmid}
	{}
	auto enqueue(msg::in::msg msg) -> void {
		msg_sender_.enqueue(std::move(msg));
	}
	[[nodiscard]]
	auto get_shmid() const -> std::string_view {
		return shm_.id();
	}
	[[nodiscard]]
	auto receive_msgs() -> std::vector<msg::out::msg> {
		auto fn = [&shm = shm_](std::byte* bytes, size_t count) -> size_t {
			return shm.receive_bytes(bytes, count);
		};
		return msg_receiver_.receive(fn);
	}
	auto send_msgs() -> void {
		auto fn = [&shm = shm_](const std::byte* bytes, size_t count) -> size_t {
			return shm.send_bytes(bytes, count);
		};
		return msg_sender_.send(fn);
	}
private:
	shm::sandbox shm_;
	msg::sender<msg::in::msg> msg_sender_;
	msg::receiver<msg::out::msg> msg_receiver_;
};

struct group_external {
	using shptr = std::shared_ptr<group_external>;
	shm::group shm;
	uint64_t epoch = 0;
};

struct device {
	id::device id;
	id::plugin plugin;
	id::sandbox sbox;
	scuff_plugin_type type;
	ext::id::plugin plugin_ext_id;
	immer::box<std::string> error;
	immer::box<std::string> name;
	device_external::shptr external;
};

struct sandbox {
	id::sandbox id;
	id::group group;
	immer::box<std::string> error;
	immer::set<id::device> devices;
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
	id::plugfile plugfile;
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

struct data {
	std::string              instance_id;
	scuff_callbacks          callbacks;
	std::jthread             poll_thread;
	std::jthread             scan_thread;

	// Copy of the model shared by non-audio threads. If a thread modifies
	// the model in a way that affects the audio thread then it should publish
	// the changes by calling publish().
	lg::plain_guarded<model> working_model;

	// Copy of the model seen by the audio thread.
	audio_data<model>        published_model;
};

static std::atomic_bool      initialized_ = false;
static std::atomic_int       id_gen_      = 0;
static std::unique_ptr<data> DATA_;

[[nodiscard]] static
auto add_device_to_sandbox(model&& m, id::sandbox sbox, id::device dev) -> model {
	m.sandboxes = m.sandboxes.update_if_exists(sbox, [dev](scuff::sandbox s) {
		s.devices = s.devices.insert(dev);
		return s;
	});
	return m;
}

[[nodiscard]] static
auto add_sandbox_to_group(model&& m, id::group group, id::sandbox sbox) -> model {
	m.groups = m.groups.update_if_exists(group, [sbox](scuff::group g) {
		g.sandboxes = g.sandboxes.insert(sbox);
		return g;
	});
	return m;
}

[[nodiscard]] static
auto erase_device(model&& m, id::device id) -> model {
	m.devices = m.devices.erase(id);
	return m;
}

[[nodiscard]] static
auto erase_group(model&& m, id::group id) -> model {
	m.groups = m.groups.erase(id);
	return m;
}

[[nodiscard]] static
auto erase_sandbox(model&& m, id::sandbox id) -> model {
	m.sandboxes = m.sandboxes.erase(id);
	return m;
}

[[nodiscard]] static
auto insert_device(model&& m, device dev) -> model {
	m.devices = m.devices.insert(std::move(dev));
	return m;
}

[[nodiscard]]
auto insert_group(model&& m, group g) -> model {
	m.groups = m.groups.insert(std::move(g));
	return m;
}

[[nodiscard]] static
auto insert_plugfile(model&& m, plugfile pf) -> model {
	m.plugfiles = m.plugfiles.insert(std::move(pf));
	return m;
}

[[nodiscard]] static
auto insert_plugin(model&& m, plugin p) -> model {
	m.plugins = m.plugins.insert(std::move(p));
	return m;
}

[[nodiscard]] static
auto insert_sandbox(model&& m, scuff::sandbox sbox) -> model {
	m.sandboxes = m.sandboxes.insert(std::move(sbox));
	return m;
}

[[nodiscard]] static
auto remove_device_from_sandbox(model&& m, id::sandbox sbox, id::device dev) -> model {
	m.sandboxes = m.sandboxes.update_if_exists(sbox, [dev](scuff::sandbox s) {
		s.devices = s.devices.erase(dev);
		return s;
	});
	return m;
}

[[nodiscard]] static
auto remove_sandbox_from_group(model&& m, id::group group, id::sandbox sbox) -> model {
	m.groups = m.groups.update_if_exists(group, [sbox](scuff::group g) {
		g.sandboxes = g.sandboxes.erase(sbox);
		return g;
	});
	return m;
}

[[nodiscard]] static
auto set_error(model&& m, id::device id, std::string_view error) -> model {
	m.devices = m.devices.update_if_exists(id, [error](scuff::device dev) {
		dev.error = error;
		return dev;
	});
	return m;
}

template <typename UpdateFn> static
auto update(UpdateFn&& fn) -> void {
	const auto m = DATA_->working_model.lock();
	*m = fn(std::move(*m));
}

static auto erase_device(id::device id) -> void { update([id](model&& m) { return erase_device(std::move(m), id); }); } 
static auto erase_group(id::group id) -> void { update([id](model&& m) { return erase_group(std::move(m), id); }); }
static auto erase_sandbox(id::sandbox id) -> void { update([id](model&& m) { return erase_sandbox(std::move(m), id); }); }
static auto insert_device(device dev) -> void { update([dev](model&& m) mutable { return insert_device(std::move(m), std::move(dev)); }); }
static auto insert_group(group g) -> void { update([g](model&& m) mutable { return insert_group(std::move(m), std::move(g)); }); }
static auto insert_plugfile(plugfile pf) -> void { update([pf](model&& m) mutable { return insert_plugfile(std::move(m), std::move(pf)); }); }
static auto insert_plugin(plugin p) -> void { update([p](model&& m) mutable { return insert_plugin(std::move(m), std::move(p)); }); }
static auto insert_sandbox(sandbox sbox) -> void { update([sbox](model&& m) mutable { return insert_sandbox(std::move(m), std::move(sbox)); }); }
static auto set_error(id::device id, std::string_view error) -> void { update([id, error](model&& m) { return set_error(std::move(m), id, error); }); }

} // scuff
