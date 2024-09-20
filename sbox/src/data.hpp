#pragma once

#include "clap_data.hpp"
#include "common/audio_sync.hpp"
#include "common/shm.hpp"
#include "common/slot_buffer.hpp"
#include "options.hpp"
#include <boost/static_string.hpp>
#include <cs_plain_guarded.h>
#include <immer/box.hpp>
#include <immer/table.hpp>
#include <memory>

namespace scuff::sbox {

struct device_flags {
	enum e {
		has_gui                    = 1 << 0,
		has_params                 = 1 << 1,
		was_created_successfully   = 1 << 2,
		nappgui_window_was_resized = 1 << 3,
	};
	int value = 0;
};

struct device_ui {
	Panel* panel;
	View* view;
	Window* window;
};

struct port_conn {
	id::device other_device;
	size_t other_port_index;
	bool outside = false;
};

struct device {
	id::device id;
	device_flags flags;
	device_ui ui;
	scuff_plugin_type type;
	immer::box<std::string> name;
	immer::vector<port_conn> input_conns;
	immer::vector<port_conn> output_conns;
	std::shared_ptr<shm::device> shm;
};

struct outside_device {
	id::device id;
	std::shared_ptr<shm::device> shm;
};

struct model {
	immer::table<device> devices;
	immer::table<clap::device> clap_devices;
	immer::table<outside_device> outside_devices;
	immer::vector<id::device> device_processing_order;
};

struct app {
	std::string                 instance_id;
	sbox::options               options;
	shm::group                  shm_group;
	shm::sandbox                shm_sbox;
	std::jthread                audio_thread;
	msg::sender<msg::out::msg>  msg_sender;
	msg::receiver<msg::in::msg> msg_receiver;
	std::atomic<uint64_t>       uid = 0;
	std::atomic_bool            schedule_terminate = false;
	std::thread::id             main_thread_id;
	audio_sync<model>           model;
};

} // scuff::sbox
