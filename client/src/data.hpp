#pragma once

#include "client.hpp"
#include "common-audio-sync.hpp"
#include "common-shm.hpp"
#include "common-slot-buffer.hpp"
#include "report-types.hpp"
#include <atomic>
#include <boost/asio.hpp>
#pragma warning(push, 0)
#include <immer/box.hpp>
#include <immer/map.hpp>
#include <immer/set.hpp>
#include <immer/table.hpp>
#include <immer/vector.hpp>
#pragma warning(pop)

namespace basio = boost::asio;

namespace scuff {

using return_device_fns = slot_buffer<return_device>;
using return_double_fns = slot_buffer<return_double>;
using return_state_fns  = slot_buffer<return_bytes>;
using return_string_fns = slot_buffer<return_string>;
using return_void_fns   = slot_buffer<return_void>;

struct return_buffers {
	return_device_fns devices;
	return_double_fns doubles;
	return_state_fns states;
	return_string_fns strings;
	return_void_fns voids;
};

struct sandbox_services {
	bp::child proc;
	scuff::return_buffers return_buffers;
	std::atomic_int ref_count = 0;
	sandbox_services(bp::child&& proc, std::string_view shmid)
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
	auto receive_msgs_from_sandbox() -> std::vector<msg::out::msg> {
		auto fn = [&shm = shm_](std::byte* bytes, size_t count) -> size_t {
			return shm.receive_bytes_from_sandbox(bytes, count);
		};
		return msg_receiver_.receive(fn);
	}
	auto send_msgs_to_sandbox() -> void {
		auto fn = [&shm = shm_](const std::byte* bytes, size_t count) -> size_t {
			return shm.send_bytes_to_sandbox(bytes, count);
		};
		msg_sender_.send(fn);
	}
private:
	shm::sandbox shm_;
	msg::sender<msg::in::msg> msg_sender_;
	msg::receiver<msg::out::msg> msg_receiver_;
};

struct group_flags {
	enum e {
		is_active = 1 << 0,
	};
	int value = 0;
};

struct group_services {
	report::msg::group_q reporter;
	shm::group shm;
	signaling::group_local_data signaling;
	uint64_t epoch = 0;
	std::atomic_int ref_count = 0;
};

struct device_flags {
	enum e {
		created_successfully = 1 << 0,
	};
	int value = 0;
};

struct device_services {
	// Increment this any time a parameter change output
	// event is received, to signal that the last saved
	// state is now dirty.
	std::atomic_int dirty_marker = 0;
	std::atomic_int saved_marker = 0;
	std::atomic_int ref_count = 0;
	shm::device shm;
};

struct device {
	id::device id;
	id::plugin plugin;
	id::sandbox sbox;
	device_flags flags;
	plugin_type type;
	ext::id::plugin plugin_ext_id;
	immer::box<std::string> error;
	immer::box<scuff::bytes> last_saved_state;
	std::shared_ptr<device_services> services;
};

struct sandbox_flags {
	enum e {
		launched         = 1 << 0,
		confirmed_active = 1 << 1,
	};
	int value = 0;
};

struct sandbox {
	id::sandbox id;
	id::group group;
	sandbox_flags flags;
	immer::box<std::string> error;
	immer::set<id::device> devices;
	std::shared_ptr<sandbox_services> services;
};

struct group {
	id::group id;
	group_flags flags;
	double sample_rate = 0.0f;
	int total_active_sandboxes = 0;
	immer::set<id::sandbox> sandboxes;
	immer::map<id::device, id::device> cross_sbox_conns;
	std::shared_ptr<group_services> services;
};

struct plugin {
	id::plugin id;
	id::plugfile plugfile;
	plugin_type type;
	ext::id::plugin ext_id;
	immer::vector<std::string> clap_features;
	immer::box<std::string> error;
	immer::box<std::string> name;
	immer::box<std::string> vendor;
	immer::box<std::string> version;
};

struct plugfile {
	id::plugfile id;
	plugin_type type;
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
	std::string            instance_id;
	std::jthread           poll_thread;
	std::jthread           scan_thread;
	std::atomic_bool       scanning = false;
	report::msg::general_q reporter;
	audio_sync<scuff::model> model;
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
auto remove_device_from_sandbox(model&& m, id::sandbox sbox, id::device dev) -> model {
	m.sandboxes = m.sandboxes.update_if_exists(sbox, [dev](scuff::sandbox s) {
		s.devices = s.devices.erase(dev);
		return s;
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
