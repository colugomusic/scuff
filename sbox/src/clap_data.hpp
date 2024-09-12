#pragma once

#include <clap/clap.h>
#include <immer/vector.hpp>

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
	clap_id id;
};

struct device {
	id::device id;
	clap::iface_host iface_host;
	clap::iface_plugin iface_plugin;
	immer::vector<param> params;
};

} // scuff::sbox::clap