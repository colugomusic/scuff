#pragma once

#include "common/audio_sync.hpp"
#include "common/shm.hpp"
#include "options.hpp"
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

struct device {
	id::device id;
	device_flags flags;
	device_ui ui;
	immer::box<std::string> name;
};

struct model {
	immer::table<device> devices;
};

struct app {
	std::string                 instance_id;
	sbox::options               options;
	shm::sandbox                shm;
	msg::sender<msg::out::msg>  msg_sender;
	msg::receiver<msg::in::msg> msg_receiver;

	// Copy of the model shared by non-audio threads. If a thread modifies
	// the model in a way that affects the audio thread then it should publish
	// the changes by calling publish().
	lg::plain_guarded<model>    working_model;

	// Copy of the model seen by the audio thread.
	audio_data<model>           published_model;
};

} // scuff::sbox
