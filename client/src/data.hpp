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
using return_state_fns  = slot_buffer<return_state>;
using return_string_fns = slot_buffer<scuff_return_string>;

struct return_buffers {
	return_device_fns devices;
	return_double_fns doubles;
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
	std::shared_ptr<const shm::device_audio_ports> shm_audio_ports;
	std::shared_ptr<const shm::device> shm_device;
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
	immer::box<device_external> external;
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
	std::string       instance_id;
	scuff_callbacks   callbacks;
	std::jthread      poll_thread;
	std::jthread      scan_thread;
	audio_sync<model> model;
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

} // scuff
