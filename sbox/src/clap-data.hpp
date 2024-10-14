#pragma once

#include "common-events-clap.hpp"
#include <boost/container/static_vector.hpp>
#include <boost/static_string.hpp>
#include <clap/clap.h>
#include <readerwriterqueue.h>
#pragma warning(push, 0)
#include <immer/box.hpp>
#include <immer/vector.hpp>
#pragma warning(pop)

namespace bc = boost::container;

namespace scuff::sbox { struct app; };

template <typename T> using rwq = moodycamel::ReaderWriterQueue<T>;

namespace scuff::sbox::clap {

struct iface_plugin {
	const clap_plugin_t* plugin                    = nullptr;
	const clap_plugin_audio_ports_t* audio_ports   = nullptr;
	const clap_plugin_context_menu_t* context_menu = nullptr;
	const clap_plugin_gui_t* gui                   = nullptr;
	const clap_plugin_params_t* params             = nullptr;
	const clap_plugin_render_t* render             = nullptr;
	const clap_plugin_state_t* state               = nullptr;
	const clap_plugin_tail_t* tail                 = nullptr;
};

struct iface_host {
	clap_host_t host;
	clap_host_audio_ports_t audio_ports;
	clap_host_context_menu_t context_menu;
	clap_host_gui_t gui;
	clap_host_latency_t latency;
	clap_host_log_t log;
	clap_host_params_t params;
	clap_host_preset_load_t preset_load;
	clap_host_state_t state;
	clap_host_tail_t tail;
	clap_host_thread_check_t thread_check;
	clap_host_track_info_t track_info;
};

struct plugin {
	const clap_plugin_entry_t* entry     = nullptr;
	const clap_plugin_factory_t* factory = nullptr;
};

struct param {
	struct flags_t {
		enum e {
			input_gesture  = 1 << 0,
			output_gesture = 1 << 1,
		};
		int value = 0;
	};
	double gesture_in_start_value  = 0.0;
	double gesture_out_start_value = 0.0;
	clap_param_info_t info;
	flags_t flags;
};

struct device_host_data {
	sbox::app* app;
	id::device id;
};

struct device_atomic_flags {
	enum e {
		active               = 1 << 0,
		processing           = 1 << 1,
		schedule_active      = 1 << 2,
		schedule_callback    = 1 << 3,
		schedule_erase       = 1 << 4,
		schedule_restart     = 1 << 5,
		schedule_param_flush = 1 << 6,
		schedule_process     = 1 << 7,
	};
	std::atomic<int> value = 0;
};

struct audio_port_info {
	std::vector<clap_audio_port_info_t> inputs;
	std::vector<clap_audio_port_info_t> outputs;
};

struct audio_buffers_detail {
	std::vector<std::vector<float*>> arrays;
	std::vector<clap_audio_buffer_t> buffers;
};

struct audio_buffers {
	audio_buffers_detail inputs;
	audio_buffers_detail outputs;
};

namespace device_msg { ///////////////////////////////////////////

struct gui_closed { bool destroyed; };
struct gui_request_hide {};
struct gui_request_resize { uint32_t width; uint32_t height; };
struct gui_request_show {};
struct gui_resize_hints_changed {};
struct log_begin{clap_log_severity severity;};
struct log_end{};
struct log_text{ static constexpr size_t MAX = 64; boost::static_string<MAX> text;};

using msg = std::variant<
	gui_closed,
	gui_request_hide,
	gui_request_resize,
	gui_request_show,
	gui_resize_hints_changed,
	log_begin,
	log_end,
	log_text
>;

using q = rwq<msg>;

} // device_msg /////////////////////////////////////////////////

struct device_service_data;

struct event_queue_context {
	device_service_data* service_data;
};

struct device_service_audio {
	clap::audio_buffers buffers;
	clap_input_events_t input_events;
	clap_output_events_t output_events;
	clap_process_t process;
};

struct device_log_collector {
	std::optional<clap_log_severity> severity;
	std::vector<boost::static_string<device_msg::log_text::MAX>> chunks;
};

struct device_service_data {
	device_atomic_flags atomic_flags;
	device_host_data host_data;
	device_msg::q msg_q;
	device_log_collector log_collector;
	scuff::events::clap::event_buffer input_event_buffer;
	scuff::events::clap::event_buffer output_event_buffer;
	event_queue_context input_events_context;
	event_queue_context output_events_context;
};

struct device_service {
	std::shared_ptr<device_service_data> data;
	std::shared_ptr<const clap::device_service_audio> audio;
	immer::box<clap::audio_port_info> audio_port_info;
};

struct iface {
	clap::iface_host host;
	clap::iface_plugin plugin;
};

struct device {
	id::device id;
	immer::box<clap::iface> iface;
	immer::box<std::string> name;
	immer::vector<param> params;
	device_service service;
};

} // scuff::sbox::clap