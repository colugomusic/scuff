#pragma once

#include "client.h"
#include "common/audio_sync.hpp"
#include "common/shm.hpp"
#include "common/slot_buffer.hpp"
#include <boost/asio.hpp>
#include <cs_plain_guarded.h>
#include <immer/box.hpp>
#include <immer/set.hpp>
#include <immer/table.hpp>

namespace basio = boost::asio;

namespace scuff {

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

struct data {
	shm::string_buffer       shm_strings;
	shm::segment_remover     shm_strings_remover;
	std::string              instance_id;
	std::string              sandbox_exe_path;
	std::string              scanner_exe_path;
	scuff_callbacks          callbacks;
	basio::io_context        io_context;
	std::jthread             io_thread;
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

} // scuff
