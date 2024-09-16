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
	bool external = false;
};

struct device_external {
	std::shared_ptr<shm::device_audio_ports> shm_audio_ports;
	std::shared_ptr<shm::device_param_info>  shm_param_info;
	std::shared_ptr<shm::device> shm_device;
};

struct device {
	id::device id;
	device_flags flags;
	device_ui ui;
	scuff_plugin_type type;
	immer::box<std::string> name;
	immer::vector<port_conn> input_conns;
	immer::vector<port_conn> output_conns;
	immer::box<device_external> ext;
};

struct model {
	immer::table<device> devices;
	immer::table<clap::device> clap_devices;
};

struct app {
	std::string                 instance_id;
	sbox::options               options;
	shm::sandbox                shm;
	std::jthread                audio_thread;
	msg::sender<msg::out::msg>  msg_sender;
	msg::receiver<msg::in::msg> msg_receiver;
	std::atomic<uint64_t>       uid = 0;
	std::thread::id             main_thread_id;

	// Copy of the model shared by non-audio threads. If a thread modifies
	// the model in a way that affects the audio thread then it should
	// cublish the changes.
	lg::plain_guarded<model>    working_model;

	// Copy of the model seen by the audio thread.
	audio_data<model>           published_model;
};

} // scuff::sbox
