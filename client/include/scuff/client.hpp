#pragma once

#include "common/constants.hpp"
#include "common/events.hpp"
#include "common/inplace_function.hpp"
#include "common/param_info.hpp"
#include "common/plugin_type.hpp"
#include "common/render_mode.hpp"
#include "common/types.hpp"
#include <any>
#include <string_view>
#include <vector>

namespace scuff {

namespace fn_sig {

using write_floats = auto (float* floats) -> void;
using read_floats  = auto (const float* floats) -> void;
using get_count    = auto (void) -> size_t;
using get_event    = auto (size_t index) -> scuff::event;
using push_event   = auto (const scuff::event& event) -> void;

} // fn_sig

template <typename Signature> using stack_fn = stdext::inplace_function<Signature, STACK_FN_CAPACITY>;

enum group_flags {
	group_flag_no_reporting = 1 << 0, // Set this if you intend to ignore all reports from this group.
};

enum scan_flags {
	scan_flag_reload_failed_devices = 1 << 0, // If a plugin is scanned which wasn't previously known,
	                                          // and the user already tried to create a device with that
	                                          // plugin ID, try to create the device again now that the
	                                          // plugin is known.
};

struct audio_writer {
	size_t port_index;
	stack_fn<fn_sig::write_floats> write;
};

struct audio_reader {
	size_t port_index;
	stack_fn<fn_sig::read_floats> read;
};

struct event_writer {
	stack_fn<fn_sig::get_count> count;
	stack_fn<fn_sig::get_event> get;
};

struct event_reader {
	// Must be able to push at least SCUFF_EVENT_PORT_SIZE events.
	// Otherwise events will be dropped.
	stack_fn<fn_sig::push_event> push;
};

struct audio_writers  { size_t count; const scuff::audio_writer* writers; };
struct audio_readers  { size_t count; const scuff::audio_reader* readers; };
struct input_device   { id::device dev; scuff::audio_writers audio_writers; scuff::event_writer event_writer; };
struct output_device  { id::device dev; scuff::audio_readers audio_readers; scuff::event_reader event_reader; };
struct input_devices  { size_t count; const scuff::input_device* devices; };
struct output_devices { size_t count; const scuff::output_device* devices; };

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
using return_bytes             = std::function<void(const std::vector<std::byte>& bytes)>;
using return_device            = std::function<void(id::device dev, bool success)>;
using return_double            = std::function<void(double value)>;
using return_string            = std::function<void(std::string_view text)>;

struct general_reporter {
	scuff::on_error on_error;
	scuff::on_plugfile_broken on_plugfile_broken;
	scuff::on_plugfile_scanned on_plugfile_scanned;
	scuff::on_plugin_broken on_plugin_broken;
	scuff::on_plugin_scanned on_plugin_scanned;
	scuff::on_scan_complete on_scan_complete;
	scuff::on_scan_error on_scan_error;
	scuff::on_scan_started on_scan_started;
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
auto audio_process(group_process process) -> void;

/////////////////////////////////////////////////////////////////////////////////////////
// The rest of these functions are thread-safe, but NOT necessarily realtime-safe.
// 
// Some of these are asynchronous, so instead of returning a value, you have to provide a
// function for them to call when they're done. These functions are written this way
// because they need to send a message to the sandbox process to do their work.
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
// If you don't call this then report messages will pile up unless you set the
// group_flag_no_reporting flag when creating the group.
auto receive_report(scuff::id::group group, const group_reporter& reporter) -> void;

// Close all editor windows.
auto close_all_editors(void) -> void;

// Connect the audio output of one device to the audio input of another device.
//  - The devices don't have to belong to the same sandbox - the connections are allowed
//    to cross from one sandbox to another, within the same sandbox group.
auto connect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void;

// Create a device and add it to the sandbox asynchronously.
//  - When the operation is complete, call the given function with the device id.
//  - If the device fails to load, it will still be created, but it will be in an error
//    state.
//  - You can create a device with a plugin ID that hasn't been scanned yet. It will be
//    created in an error state and will remain that way until the plugin is found by
//    a future scan where the reload_failed_devices flag is set.
auto create_device_async(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id, return_device fn) -> id::device;

// Remove the given connection between two devices.
auto disconnect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void;

// Create a device by duplicating an existing device, and add it to the sandbox.
// - The target device can belong to a different sandbox.
auto duplicate(id::device dev, id::sandbox sbox) -> id::device;

// Create a device by duplicating an existing device, and add it to the sandbox,
// asynchronously.
// When the operation is complete, call the given function with the device id.
// - The target device can belong to a different sandbox.
auto duplicate_async(id::device dev, id::sandbox sbox, return_device fn) -> id::device;

// Erase a device.
// It's OK to do this while the audio thread is processing. The device will be
// erased when it's safe to do so.
auto erase(id::device dev) -> void;

// Get device metadata at column.
auto get_metadata(id::device dev, size_t column) -> std::any;

// Hide the device editor window.
auto gui_hide(id::device dev) -> void;

// Show the device editor window.
auto gui_show(id::device dev) -> void;

// If the device failed to load successfully, return the error string.
auto get_error(id::device dev) -> const char*;

// Return the device name.
auto get_name(id::device dev) -> const char*;

// Return the number of parameters for the given device.
auto get_param_count(id::device dev) -> size_t;

// Return the plugin type.
auto get_type(id::plugin plugin) -> plugin_type;

// Calculate the string representation of the given value asynchronously.
// When it is ready, call the given function with it.
auto get_value_text(id::device dev, idx::param param, double value, return_string fn) -> void;

// Return a list of plugin files which failed to load.
auto get_broken_plugfiles() -> std::vector<id::plugfile>;

// Return a list of plugin which failed to load.
auto get_broken_plugins() -> std::vector<id::plugin>;

// Return a list of plugins which at least appear to be working
// (they did not fail to load during plugin scanning.)
auto get_working_plugins() -> std::vector<id::plugin>;

// Return the plugin for the given device.
auto get_plugin(id::device dev) -> id::plugin;

// Return true if the device has a GUI.
auto has_gui(id::device dev) -> bool;

// Return true if the device has parameters.
auto has_params(id::device dev) -> bool;

// Load the device state asynchronously.
auto load(id::device dev, const void* bytes, size_t count) -> void;

// Save the device state asynchronously.
auto save(id::device dev, return_bytes fn) -> void;

// Set device metadata at column.
auto set_metadata(id::device dev, size_t column, std::any data) -> void;

// Set the render mode for the given device.
auto set_render_mode(id::device dev, render_mode mode) -> void;

// Return true if the device loaded successfully.
auto was_loaded_successfully(id::device dev) -> bool;

// Activate audio processing for the sandbox group.
auto activate(id::group group, double sr) -> void;

// Deactivate audio processing for the sandbox group.
auto deactivate(id::group group) -> void;

// Create a new group.
// - Every sandbox has to belong to a group.
// - This is what allows data to travel between sandboxes.
// - On failure, returns < 0.
auto create_group(void) -> id::group;

// Erase a group.
// It's OK to do this while the audio thread is processing. The group will be
// erased when it's safe to do so.
auto erase(id::group group) -> void;

// Create a new sandbox.
// - Every sandbox has to belong to a group.
// - Data can travel between sandboxes in the same group.
// - If starting the sandbox process fails, the sandbox will still be created,
//   but it will be in an error state.
auto create_sandbox(id::group group, const char* sbox_exe_path) -> id::sandbox;

// Erase a sandbox.
// It's OK to do this while the audio thread is processing. The sandbox will be
// erased when it's safe to do so.
auto erase(id::sandbox sbox) -> void;

// If the sandbox failed to start, return the error string.
auto get_error(id::sandbox sbox) -> const char*;

// Check if the given sandbox is running.
auto is_running(id::sandbox sbox) -> bool;

// Return true if the plugin scanner process is currently running.
auto is_scanning(void) -> bool;

// Find the device parameter with the given id.
// The id is either a Steinberg::Vst::ParamID or a clap_id.
// - This will return an invalid index if the device hasn't finished being created yet.
auto find(id::device dev, ext::id::param param_id) -> idx::param;

// Return the parameter info.
auto get_info(id::device dev, idx::param param) -> param_info;

// Get the current value of the parameter, asynchronously.
// When the result is ready, call the given function with it.
auto get_value(id::device dev, idx::param param, return_double fn) -> void;

// If the plugin file failed to scan, return the error string.
auto get_error(id::plugfile plugfile) -> const char*;

// Return the file path of the plugin file.
auto get_path(id::plugfile plugfile) -> const char*;

// Find a scanned plugin with the given string ID.
// Returns < 0 if the plugin was not found.
auto find(ext::id::plugin plugin_id) -> id::plugin;

// If the plugin failed to load, return the error string.
auto get_error(id::plugin plugin) -> const char*;

// Returns the plugin ID string.
auto get_ext_id(id::plugin plugin) -> ext::id::plugin;

// Returns the plugin name
auto get_name(id::plugin plugin) -> const char*;

// Returns the plugin vendor.
auto get_vendor(id::plugin plugin) -> const char*;

// Returns the plugin version string.
auto get_version(id::plugin plugin) -> const char*;

// Push a device event
auto push_event(id::device dev, const scuff::event& event) -> void;

// Restart the sandbox.
auto restart(id::sandbox sbox, const char* sbox_exe_path) -> void;

// Scan the system for plugins. If the scanner process is already
// running, it is restarted.
auto scan(const char* scan_exe_path, int flags) -> void;

} // scuff