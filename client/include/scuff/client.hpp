#pragma once

#include "common/constants.hpp"
#include "common/events.hpp"
#include "common/param_info.hpp"
#include "common/plugin_type.hpp"
#include "common/render_mode.hpp"
#include "common/types.hpp"
#include <functional>
#include <string_view>
#include <vector>

namespace scuff {

struct scan_flags {
	enum e {
		reload_failed_devices = 1 << 0, // If a plugin is scanned which wasn't previously known,
		                                // and the user already tried to create a device with that
		                                // plugin ID, try to create the device again now that the
		                                // plugin is known.
	};
	int value = 0;
};

struct audio_writer {
	size_t port_index;
	std::function<void(float* floats)> write;
};

struct audio_reader {
	size_t port_index;
	std::function<void(const float* floats)> read;
};

struct event_writer {
	std::function<size_t()> count;
	std::function<scuff::event(size_t index)> get;
};

struct event_reader {
	// Must be able to push at least scuff::EVENT_PORT_SIZE events.
	// Otherwise events will be dropped.
	std::function<void(const scuff::event& event)> push;
};

using audio_writers  = std::vector<scuff::audio_writer>;
using audio_readers  = std::vector<scuff::audio_reader>;

struct input_device  { id::device dev; scuff::audio_writers audio_writers; scuff::event_writer event_writer; };
struct output_device { id::device dev; scuff::audio_readers audio_readers; scuff::event_reader event_reader; };

using input_devices  = std::vector<scuff::input_device>;
using output_devices = std::vector<scuff::output_device>;
using bytes          = std::vector<std::byte>;

struct group_process {
	id::group group;
	scuff::input_devices input_devices;
	scuff::output_devices output_devices;
};

using on_device_error          = std::function<void(id::device dev, std::string_view error)>;
using on_device_params_changed = std::function<void(id::device dev)>;
using on_error                 = std::function<void(std::string_view error)>;
using on_plugfile_broken       = std::function<void(id::plugfile plugfile)>;
using on_plugfile_scanned      = std::function<void(id::plugfile plugfile)>;
using on_plugin_broken         = std::function<void(id::plugin plugin)>;
using on_plugin_scanned        = std::function<void(id::plugin plugin)>;
using on_sbox_crashed          = std::function<void(id::sandbox sbox, std::string_view error)>;
using on_sbox_error            = std::function<void(id::sandbox sbox, std::string_view error)>;
using on_sbox_info             = std::function<void(id::sandbox sbox, std::string_view info)>;
using on_sbox_started          = std::function<void(id::sandbox sbox)>;
using on_sbox_warning          = std::function<void(id::sandbox sbox, std::string_view warning)>;
using on_scan_complete         = std::function<void()>;
using on_scan_error            = std::function<void(std::string_view error)>;
using on_scan_started          = std::function<void()>;
using on_scan_warning          = std::function<void(std::string_view warning)>;
using return_bytes             = std::function<void(const scuff::bytes& bytes)>;
using return_device            = std::function<void(id::device dev, bool load_success)>;
using return_double            = std::function<void(double value)>;
using return_string            = std::function<void(std::string_view text)>;
using return_void              = std::function<void(void)>;

struct general_reporter {
	scuff::on_error on_error;
	scuff::on_plugfile_broken on_plugfile_broken;
	scuff::on_plugfile_scanned on_plugfile_scanned;
	scuff::on_plugin_broken on_plugin_broken;
	scuff::on_plugin_scanned on_plugin_scanned;
	scuff::on_scan_complete on_scan_complete;
	scuff::on_scan_error on_scan_error;
	scuff::on_scan_started on_scan_started;
	scuff::on_scan_warning on_scan_warning;
};

struct group_reporter {
	scuff::on_device_error on_device_error;
	scuff::on_device_params_changed on_device_params_changed;
	scuff::on_error on_error;
	scuff::on_sbox_crashed on_sbox_crashed;
	scuff::on_sbox_error on_sbox_error;
	scuff::on_sbox_info on_sbox_info;
	scuff::on_sbox_started on_sbox_started;
	scuff::on_sbox_warning on_sbox_warning;
};

/////////////////////////////////////////////////////////////////////////////////////////
// Audio thread
/////////////////////////////////////////////////////////////////////////////////////////

// Process the sandbox group. This is safe to call in a realtime thread.
auto audio_process(const group_process& process) -> void;

/////////////////////////////////////////////////////////////////////////////////////////
// The rest of these functions are thread-safe, but NOT necessarily realtime-safe.
// 
// Functions with the *_async suffix return immediately and call the provided function
// with the result when the operation is complete.
// In a UI processing thread, it is recommended to use the asynchronous functions over
// their blocking variants when possible because these operations require a
// back-and-forth between the client and sandbox processes in order to do their work.
/////////////////////////////////////////////////////////////////////////////////////////

// Call this before anything else.
//  - If an error occurs during initialization then the error callback will be called.
//  - Returns true if the initialization was successful, or if scuff was already
//    initialized.
auto init(const scuff::on_error& on_error) -> bool;

// Call this when you're done with the sandboxing system.
//  - Don't call anything else after this, except for init() to reinitialize.
auto shutdown(void) -> void;

// Call this periodically to receive general report messages for the sandboxing system.
auto receive_report(const general_reporter& reporter) -> void;

// Call this periodically to receive report messages for the group.
// In a simple audio application with one audio thread and one UI thread you might call
// this in your UI message loop.
// If you are doing offline processing in a background thread then maybe you could call
// this once per audio buffer.
// If you don't call this then report messages will pile up and consume memory.
auto receive_report(scuff::id::group group, const group_reporter& reporter) -> void;

// Activate audio processing for the sandbox group.
auto activate(id::group group, double sr) -> void;

// Deactivate audio processing for the sandbox group.
auto deactivate(id::group group) -> void;

// Close all editor windows.
auto close_all_editors(void) -> void;

// Connect the audio output of one device to the audio input of another device.
//  - The devices don't have to belong to the same sandbox - the connections are allowed
//    to cross from one sandbox to another, within the same sandbox group.
auto connect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void;

// Create a device and add it to the sandbox.
//  - If the device fails to load, it will still be created, but it will be in an error
//    state.
//  - You can create a device with a plugin ID that hasn't been scanned yet. It will be
//    created in an error state and will remain that way until the plugin is found by
//    a future scan where the reload_failed_devices flag is set.
auto create_device(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id) -> id::device;

// Create a device and add it to the sandbox asynchronously.
//  - When the operation is complete, call the given function with the device id.
auto create_device_async(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id, return_device fn) -> id::device;

// Create a new group.
// - Every sandbox has to belong to a group.
// - This is what allows data to travel between sandboxes.
// - On failure, returns an invalid id
auto create_group(double sample_rate) -> id::group;

// Create a new sandbox.
// - Every sandbox has to belong to a group.
// - Data can travel between sandboxes in the same group.
// - If starting the sandbox process fails, the sandbox will still be created,
//   but it will be in an error state.
auto create_sandbox(id::group group, std::string_view sbox_exe_path) -> id::sandbox;

// Remove the given connection between two devices.
auto disconnect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void;

// Create a device by duplicating an existing device, and add it to the sandbox.
// - The target device can belong to a different sandbox.
// - The target device can belong to a different group.
auto duplicate(id::device dev, id::sandbox sbox) -> id::device;

// Duplicate a device asynchronously.
// - When the operation is complete, call the given function with the device id.
auto duplicate_async(id::device dev, id::sandbox sbox, return_device fn) -> id::device;

// Erase a device.
// It's OK to do this while the audio thread is processing. The internal device
// data will be erased when it's safe to do so.
auto erase(id::device dev) -> void;

// Erase a group.
// It's OK to do this while the audio thread is processing. The internal group
// data will be erased when it's safe to do so.
auto erase(id::group group) -> void;

// Erase a sandbox.
// It's OK to do this while the audio thread is processing. The internal sandbox
// data will be erased when it's safe to do so.
auto erase(id::sandbox sbox) -> void;

// Find the device parameter with the given id.
// The id is either a Steinberg::Vst::ParamID or a clap_id.
// - This will return an invalid index if the device hasn't finished being created yet.
auto find(id::device dev, ext::id::param param_id) -> idx::param;

// Find a scanned plugin with the given string ID.
// Returns < 0 if the plugin was not found.
auto find(ext::id::plugin plugin_id) -> id::plugin;

// Return a list of plugin files which failed to load.
auto get_broken_plugfiles() -> std::vector<id::plugfile>;

// Return a list of plugins which failed to load.
auto get_broken_plugins() -> std::vector<id::plugin>;

// If the plugin file failed to scan, return the error string.
auto get_error(id::plugfile plugfile) -> const char*;

// If the plugin failed to load, return the error string.
auto get_error(id::plugin plugin) -> const char*;

// If the sandbox failed to start, return the error string.
auto get_error(id::sandbox sbox) -> const char*;

// Returns the plugin ID string.
auto get_ext_id(id::plugin plugin) -> ext::id::plugin;

// Return the parameter info.
auto get_info(id::device dev, idx::param param) -> param_info;

// Returns the plugin name
auto get_name(id::plugin plugin) -> const char*;

// Return the file path of the plugin file.
auto get_path(id::plugfile plugfile) -> const char*;

// Get the current value of the parameter.
auto get_value(id::device dev, idx::param param) -> double;

// Get the current value of the parameter, asynchronously.
//  - When the result is ready, call the given function with it.
auto get_value_async(id::device dev, idx::param param, return_double fn) -> void;

// Returns the plugin vendor.
auto get_vendor(id::plugin plugin) -> const char*;

// Returns the plugin version string.
auto get_version(id::plugin plugin) -> const char*;

// If the device failed to load successfully, return the error string.
auto get_error(id::device dev) -> const char*;

// For CLAP plugins, return the list of feature strings.
// Not sure how this will look for VST yet.
// This might change.
auto get_features(id::plugin plugin) -> std::vector<std::string>;

// Return the number of parameters for the given device.
auto get_param_count(id::device dev) -> size_t;

// Return the plugin for the given device.
auto get_plugin(id::device dev) -> id::plugin;

// Returns the plugin ID string for the given device.
auto get_plugin_ext_id(id::device dev) -> ext::id::plugin;

// Return the plugin type.
auto get_type(id::plugin plugin) -> plugin_type;

// Calculate the string representation of the given value.
auto get_value_text(id::device dev, idx::param param, double value) -> std::string;

// Calculate the string representation of the given value, asynchronously.
//  - When it is ready, call the given function with it.
auto get_value_text_async(id::device dev, idx::param param, double value, return_string fn) -> void;

// Return a list of plugins which at least appear to be working
// (they did not fail to load during plugin scanning.)
auto get_working_plugins() -> std::vector<id::plugin>;

// Hide the device editor window.
auto gui_hide(id::device dev) -> void;

// Show the device editor window.
auto gui_show(id::device dev) -> void;

// Return true if the device has a GUI.
auto has_gui(id::device dev) -> bool;

// Return true if the device has parameters.
auto has_params(id::device dev) -> bool;

// Return true if this plugin is suitable for use in a "rack", for example it is an audio
// effect or analyzer of some kind.
// For CLAP this is going to check the plugin features for "audio-effect" or "analyzer".
// I don't know what we will do for VSTs yet.
// I'm not sure I like this function so I might replace it with something else. Let me know
// what you think.
auto has_rack_features(id::plugin plugin) -> bool;

// Check if the given sandbox is running.
auto is_running(id::sandbox sbox) -> bool;

// Return true if the plugin scanner process is currently running.
auto is_scanning(void) -> bool;

// Load the device state and block until the operation is complete.
auto load(id::device dev, const scuff::bytes& bytes) -> void;

// Load the device state, asynchronously.
//  - When the operation is complete, call the given function.
auto load_async(id::device dev, const scuff::bytes& bytes, return_void fn) -> void;

// Push a device event
auto push_event(id::device dev, const scuff::event& event) -> void;

// Restart the sandbox.
auto restart(id::sandbox sbox, std::string_view sbox_exe_path) -> void;

// Save the device state.
auto save(id::device dev) -> scuff::bytes;

// Save the device state, asynchronously.
//  - When the operation is complete, call the given function with the result.
auto save_async(id::device dev, return_bytes fn) -> void;

// Scan the system for plugins. If the scanner process is already
// running, it is restarted.
auto scan(std::string_view scan_exe_path, scan_flags flags) -> void;

// Set the render mode for the given group.
auto set_render_mode(id::group group, render_mode mode) -> void;

// Return true if the device loaded successfully.
auto was_loaded_successfully(id::device dev) -> bool;

} // scuff
