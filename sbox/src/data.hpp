#pragma once

#include "clap_data.hpp"
#include "common/audio_sync.hpp"
#include "common/plugin_type.hpp"
#include "common/events.hpp"
#include "common/slot_buffer.hpp"
#include "options.hpp"
#include <boost/static_string.hpp>
#include <cs_plain_guarded.h>
#include <memory>
#pragma warning(push, 0)
#include <immer/box.hpp>
#include <immer/flex_vector.hpp>
#include <immer/table.hpp>
#pragma warning(pop)

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
	size_t this_port_index;
	size_t other_port_index;
	auto operator<=>(const port_conn&) const = default;
};

struct device_service {
	immer::box<shm::device> shm;
	std::shared_ptr<rwq<scuff::event>> input_events_from_main = std::make_shared<rwq<scuff::event>>(EVENT_PORT_SIZE);
};

struct device {
	id::device id;
	device_flags flags;
	device_ui ui;
	plugin_type type;
	double sample_rate = 0.0;
	immer::box<std::string> name;
	immer::flex_vector<port_conn> output_conns;
	device_service service;
};

struct model {
	immer::table<device> devices;
	immer::table<clap::device> clap_devices;
	immer::vector<id::device> device_processing_order;
};

struct app {
	std::string                        instance_id;
	sbox::options                      options;
	shm::group                         shm_group;
	shm::sandbox                       shm_sbox;
	std::jthread                       audio_thread;
	msg::sender<msg::out::msg>         msg_sender;
	msg::receiver<msg::in::msg>        msg_receiver;
	std::thread::id                    main_thread_id;
	audio_sync<sbox::model>            model;
	std::shared_ptr<const sbox::model> audio_model;
	std::atomic<uint64_t>              uid = 0;
	std::atomic_bool                   schedule_terminate = false;
};

} // scuff::sbox
