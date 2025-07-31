#pragma once

#include "client.hpp"
#include "common-message-send-rcv.hpp"
#include "common-shm.hpp"
#include "common-slot-buffer.hpp"
#include "jthread.hpp"
#include "ui-types.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <ez.hpp>
#pragma warning(push, 0)
#include <immer/box.hpp>
#include <immer/map.hpp>
#include <immer/set.hpp>
#include <immer/table.hpp>
#include <immer/vector.hpp>
#pragma warning(pop)

namespace basio = boost::asio;

namespace scuff {

using return_device_create_result_fns = slot_buffer<return_create_device_result>;
using return_device_load_result_fns   = slot_buffer<return_load_device_result>;
using return_double_fns               = slot_buffer<return_double>;
using return_state_fns                = slot_buffer<return_bytes>;
using return_string_fns               = slot_buffer<return_string>;

struct return_buffers {
	return_device_create_result_fns device_create_results;
	return_device_load_result_fns   device_load_results;
	return_double_fns               doubles;
	return_state_fns                states;
	return_string_fns               strings;
};

struct sandbox_service {
	bp::v1::child proc;
	scuff::return_buffers return_buffers;
	std::atomic_int ref_count = 0;
	shm::sandbox shm;
	sandbox_service(bp::v1::child&& proc, std::string_view shmid)
		: proc{std::move(proc)}
		, shm{shm::create_sandbox(shmid, true)}
	{}
	auto enqueue(msg::in::msg msg) -> void {
		msg_sender_.enqueue(std::move(msg));
	}
	[[nodiscard]]
	auto get_shmid() const -> std::string_view {
		return shm.seg.id;
	}
	[[nodiscard]]
	auto receive_msgs_from_sandbox() -> const std::vector<msg::out::msg>& {
		auto fn = [&shm = this->shm](std::byte* bytes, size_t count) -> size_t {
			return shm::receive_bytes_from_sandbox(shm, bytes, count);
		};
		return msg_receiver_.receive(fn);
	}
	auto send_msgs_to_sandbox() -> void {
		auto fn = [&shm = this->shm](const std::byte* bytes, size_t count) -> size_t {
			return shm::send_bytes_to_sandbox(shm, bytes, count);
		};
		msg_sender_.send(fn);
	}
private:
	msg::sender<msg::in::msg> msg_sender_;
	msg::receiver<msg::out::msg> msg_receiver_;
};

struct group_flags {
	enum e {
		is_active         = 1 << 0,
		marked_for_delete = 1 << 1,
	};
	int value = 0;
};

struct group_service {
	ui::group_q ui;
	shm::group shm;
	signaling::group_local_data signaling;
	signaling::clientside_group signaler;
	std::atomic_int ref_count = 0;
};

struct client_device_flags {
	enum e {
		has_remote = 1 << 0, // If set, this means the device has an active
		                     // 'remote' counterpart in a sandbox process.
		has_gui    = 1 << 1,
		has_params = 1 << 2,
	};
	int value = 0;
};

struct device_service {
	// Increment this any time a parameter change output
	// event is received, to signal that the last saved
	// state is now dirty.
	std::atomic_int ref_count = 0;
	shm::device shm;
};

struct device {
	id::device id;
	id::plugin plugin;
	id::sandbox sbox;
	client_device_flags flags;
	plugin_type type;
	return_create_device_result creation_callback;
	uint32_t latency = 0;
	std::chrono::system_clock::duration autosave_interval = std::chrono::milliseconds{DEFAULT_AUTOSAVE_MS};
	return_bytes autosave_callback;
	void* editor_window_native_handle = nullptr;
	ext::id::plugin plugin_ext_id;
	immer::box<std::string> error;
	immer::box<scuff::bytes> last_saved_state;
	immer::vector<client_param_info> param_info;
	device_port_info port_info;
	std::shared_ptr<device_service> service;
};

struct sandbox_flags {
	enum e {
		launched          = 1 << 0,
		confirmed_active  = 1 << 1,
		marked_for_delete = 1 << 2,
	};
	int value = 0;
};

struct sandbox {
	id::sandbox id;
	id::group group;
	sandbox_flags flags;
	immer::set<id::device> devices;
	std::shared_ptr<sandbox_service> service;
};

struct cross_sbox_connection {
	id::device out_dev_id;
	id::device in_dev_id;
	size_t out_port;
	size_t in_port;
};

struct group {
	id::group id;
	group_flags flags;
	double sample_rate = 0.0f;
	void* parent_window_handle = nullptr;
	int total_active_sandboxes = 0;
	scuff::render_mode render_mode = scuff::render_mode::realtime;
	immer::set<id::sandbox> sandboxes;
	immer::set<cross_sbox_connection> cross_sbox_conns;
	std::shared_ptr<group_service> service;
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
	bool has_gui = false;
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
	std::thread::id        ui_thread_id;
	std::atomic_bool       scanning = false;
	ui::general_q          ui;
	ez::sync<scuff::model> model;
};

static std::atomic_bool      initialized_ = false;
static std::atomic_int       id_gen_      = 0;
static std::unique_ptr<data> DATA_;

[[nodiscard]] static
auto operator==(const cross_sbox_connection& lhs, const cross_sbox_connection& rhs) -> bool {
	return lhs.out_dev_id == rhs.out_dev_id
	    && lhs.in_dev_id == rhs.in_dev_id
	    && lhs.out_port == rhs.out_port
	    && lhs.in_port == rhs.in_port;
}

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

namespace std {

template <> struct hash<scuff::cross_sbox_connection> {
	[[nodiscard]] auto operator()(const scuff::cross_sbox_connection& conn) const -> size_t {
		size_t seed = 0;
		boost::hash_combine(seed, conn.out_dev_id.value);
		boost::hash_combine(seed, conn.in_dev_id.value);
		boost::hash_combine(seed, conn.out_port);
		boost::hash_combine(seed, conn.in_port);
		return seed;
	}
};

} // std
