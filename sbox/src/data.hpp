#pragma once

#include "clap-data.hpp"
#include "common-message-send-rcv.hpp"
#include "common-param-info.hpp"
#include "common-plugin-type.hpp"
#include "common-events.hpp"
#include "common-shm.hpp"
#include "common-slot-buffer.hpp"
#include "jthread.hpp"
#include "options.hpp"
#include "window-size.hpp"
#include <boost/static_string.hpp>
#include <cs_plain_guarded.h>
#include <edwin.hpp>
#include <ez.hpp>
#include <memory>
#pragma warning(push, 0)
#include <immer/box.hpp>
#include <immer/flex_vector.hpp>
#include <immer/table.hpp>
#pragma warning(pop)

namespace scuff::sbox {

struct create_gui_result {
	bool success    = false;
	bool resizable  = false;
	uint32_t width  = 0;
	uint32_t height = 0;
};

struct device_ui {
	edwin::window* window = nullptr;
};

struct port_conn {
	id::device other_device;
	size_t this_port_index;
	size_t other_port_index;
	auto operator<=>(const port_conn&) const = default;
};

struct device_service {
	shm::device shm;
	std::optional<window_size_f> scheduled_window_resize;
	rwq<scuff::event> input_events_from_main = rwq<scuff::event>(EVENT_PORT_SIZE);
	std::chrono::steady_clock::time_point next_save = std::chrono::steady_clock::now();
	std::atomic_int dirty_marker    = 0;
	std::atomic_int autosave_marker = 0;
};

struct device {
	id::device id;
	device_flags flags;
	device_ui ui;
	plugin_type type;
	double sample_rate = 0.0;
	std::chrono::steady_clock::duration autosave_interval = std::chrono::milliseconds{DEFAULT_AUTOSAVE_MS};
	std::optional<rgba32> track_color;
	immer::box<std::string> track_name;
	immer::box<std::string> name;
	immer::flex_vector<port_conn> output_conns;
	immer::vector<scuff::sbox_param_info> param_info;
	std::shared_ptr<device_service> service = std::make_shared<device_service>();
};

struct model {
	immer::table<device> devices;
	immer::table<clap::device> clap_devices;
	immer::vector<id::device> device_processing_order;
};

using heartbeat_time = std::chrono::time_point<std::chrono::steady_clock>;

enum class mode {
	invalid,
	gui_test,
	sandbox,
	test
};

struct icon {
	std::vector<edwin::rgba> pixels;
	edwin::size size;
};

struct app {
	sbox::options                     options;
	sbox::mode                        mode;
	scuff::render_mode                render_mode = scuff::render_mode::realtime;
	shm::group                        shm_group;
	shm::sandbox                      shm_sbox;
	signaling::sandboxside_group      group_signaler;
	signaling::sandboxside_sandbox    sandbox_signaler;
	std::jthread                      audio_thread;
	msg::sender<msg::out::msg>        client_msg_sender;
	msg::receiver<msg::in::msg>       client_msg_receiver;
	lg::plain_guarded<msg::out::buf>  msgs_out;
	std::thread::id                   main_thread_id;
	ez::sync<sbox::model>             model;
	ez::immutable<sbox::model>        audio_model;
	std::atomic<uint64_t>             uid = 0;
	std::atomic_bool                  schedule_terminate = false;
	bool                              active = false;
	double                            sample_rate = 44100.0;
	heartbeat_time                    last_heartbeat;
	msg::out::buf                     reusable_msg_out_buf;
	sbox::icon                        window_icon;
};

} // scuff::sbox
