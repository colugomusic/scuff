#pragma once

#include "common-colors.hpp"
#include "common-constants.hpp"
#include "common-device-info.hpp"
#include "common-events.hpp"
#include "common-param-info.hpp"
#include "common-plugin-type.hpp"
#include "common-render-mode.hpp"
#include "common-types.hpp"
#include <functional>
#include <optional>
#include <stdexcept>
#include <source_location>
#include <string_view>
#include <vector>

namespace scuff {

// OBJECT LIFETIMES
// ----------------
// - Sandboxes can have devices.
// - Groups can have sandboxes.
// - If erase(sandbox) is called, and it still has at least one device, it will be marked for delete and then
//   automatically deleted when its final device is deleted. Otherwise it will be deleted immediately.
// - If erase(group) is called, and it still has at least one sandbox, it will be marked for delete and then
//   automatically deleted when its final sandbox is deleted. Otherwise it will be deleted immediately.

// WHEN ARE EXCEPTIONS THROWN?
// ---------------------------
// Generally speaking this library can throw in the following situations:
// - An invalid input, e.g. passing in an invalid ID.
// - Calling ui_update() without providing all of the callbacks.
// - Forgetting to call init().
// - Attempting to connect two devices which exist in different groups.
// - A bug in the sandboxing system.
//
// This library does NOT throw in any of these situations:
// - An error occurring during plugin scanning (this is reported via a callback instead.)
// - A sandbox crashing (this is reported via a callback instead.)
// - A device failing to load correctly (the device is created, but remains in an "unloaded" state.)
// - One of the 'find(...)' overloads failing to find the requested thing (a special 'invalid' value is
//   returned instead, to indicate failure.)

// EXCEPTION SAFETY
// ----------------
// https://en.wikipedia.org/wiki/Exception_safety
// This library provides "strong exception safety" throughout, meaning if an exception is thrown then there will
// be no side effects. If this is ever not the case, then I consider it a bug in the sandboxing system.

// WHICH EXCEPTION TYPES ARE THROWN?
// ---------------------------------
// Only this one:
struct runtime_error : public std::runtime_error {
	runtime_error(std::string_view function_name, const char* what) : std::runtime_error(what), function_name{function_name} {}
	std::string function_name;
};

struct scan_flags {
	enum e {
		retry_failed_devices = 1 << 0, // If a plugin is scanned which wasn't previously known,
		                               // and the user already tried to create a device with that
		                               // plugin ID, try to create the device again now that the
		                               // plugin is known.
	};
	int value = 0;
};

struct create_device_result {
	id::device id;
	bool success = false;
};

struct load_device_result {
	id::device id;
	bool success = false;
};

using bytes = std::vector<std::byte>;

struct input_event {
	id::device device_id;
	scuff::event event;
};

struct output_event {
	id::device device_id;
	scuff::event event;
};

using on_device_editor_visible_changed = std::function<auto (id::device dev, bool visible, int64_t native_handle) -> void>;
using on_device_state_load             = std::function<auto (load_device_result result) -> void>;
using on_device_late_create            = std::function<auto (create_device_result result) -> void>;
using on_device_params_changed         = std::function<auto (id::device dev) -> void>;
using on_error                         = std::function<auto (std::string_view error) -> void>;
using on_plugfile_broken               = std::function<auto (id::plugfile plugfile) -> void>;
using on_plugfile_scanned              = std::function<auto (id::plugfile plugfile) -> void>;
using on_plugin_broken                 = std::function<auto (id::plugin plugin) -> void>;
using on_plugin_scanned                = std::function<auto (id::plugin plugin) -> void>;
using on_sbox_crashed                  = std::function<auto (id::sandbox sbox, std::string_view error) -> void>;
using on_sbox_error                    = std::function<auto (id::sandbox sbox, std::string_view error) -> void>;
using on_sbox_info                     = std::function<auto (id::sandbox sbox, std::string_view info) -> void>;
using on_sbox_started                  = std::function<auto (id::sandbox sbox) -> void>;
using on_sbox_warning                  = std::function<auto (id::sandbox sbox, std::string_view warning) -> void>;
using on_scan_complete                 = std::function<auto () -> void>;
using on_scan_error                    = std::function<auto (std::string_view error) -> void>;
using on_scan_started                  = std::function<auto () -> void>;
using on_scan_warning                  = std::function<auto (std::string_view warning) -> void>;
using return_bytes                     = std::function<auto (const scuff::bytes& bytes) -> void>;
using return_create_device_result      = std::function<auto (create_device_result result) -> void>;
using return_load_device_result        = std::function<auto (load_device_result result) -> void>;
using return_double                    = std::function<auto (double value) -> void>;
using return_string                    = std::function<auto (std::string_view text) -> void>;
using write_audio                      = std::function<auto (float* floats) -> void>;
using read_audio                       = std::function<auto (const float* floats) -> void>;
using get_input_events_count           = std::function<auto () -> size_t>;
using pop_input_events                 = std::function<auto (size_t count, scuff::input_event* buffer) -> size_t>;
using push_output_event                = std::function<auto (const scuff::output_event& event) -> void>;

struct audio_input {
	id::device dev_id;
	size_t port_index;
	write_audio write_to;
};

struct audio_output {
	id::device dev_id;
	size_t port_index;
	read_audio read_from;
};

struct input_events {
	get_input_events_count count;
	pop_input_events pop;
};

struct output_events {
	push_output_event push;
};

using audio_inputs  = std::vector<scuff::audio_input>;
using audio_outputs = std::vector<scuff::audio_output>;

struct group_process {
	id::group group;
	scuff::audio_inputs audio_inputs;
	scuff::audio_outputs audio_outputs;
	scuff::input_events input_events;
	scuff::output_events output_events;
};

struct general_ui {
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

struct group_ui {
	scuff::on_device_editor_visible_changed on_device_editor_visible_changed;
	scuff::on_device_state_load on_device_state_load;
	scuff::on_device_params_changed on_device_params_changed;
	scuff::on_device_late_create on_device_late_create;
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
// with the result when the operation is complete, on the next call to ui_update.
// In a UI processing thread, it is recommended to use the asynchronous functions over
// their blocking variants when possible because these operations require a
// back-and-forth between the client and sandbox processes in order to do their work.
/////////////////////////////////////////////////////////////////////////////////////////

// Call this before anything else.
//  - The thread this is called from is considered the "UI thread" until shutdown() is
//    called.
auto init() -> void;

// Call this when you're done with the sandboxing system.
//  - Don't call anything else after this, except for init() to reinitialize.
auto shutdown(void) -> void;

// Call this periodically to receive general messages for the sandboxing system.
// - If you don't call this then messages will pile up and consume memory.
auto ui_update(const general_ui& ui) -> void;

// Call this periodically to receive messages for the group.
// - In a simple audio application with one audio thread and one UI thread you might call
//   this in your UI message loop.
// - If you are doing offline processing in a background thread then maybe you could call
//   this once per audio buffer.
// - If you don't call this then messages will pile up and consume memory.
auto ui_update(scuff::id::group group, const group_ui& ui) -> void;

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

// Create a device and add it to the sandbox, synchronously.
//  - This involves a round-trip to the sandbox process.
//  - If the device fails to load, it will still be created, but it will be in an unloaded
//    state.
//  - You can create a device with a plugin ID that hasn't been scanned yet. It will be
//    created in an unloaded state and will remain that way until the plugin is found by
//    a future scan where the retry_failed_devices flag is set.
[[nodiscard]]
auto create_device(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id) -> create_device_result;

// Create a device and add it to the sandbox asynchronously.
//  - When the operation is complete, the callback will be called on the next call to ui_update(group).
//  - Before the operation is complete, the returned device ID will be valid, but the device will be in an unloaded state.
[[nodiscard]]
auto create_device_async(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id, return_create_device_result fn) -> id::device;

// Create a new group.
// - Every sandbox has to belong to a group.
// - This is what allows data to travel between sandboxes.
[[nodiscard]]
auto create_group(void* parent_window_handle) -> id::group;

// Create a new sandbox.
// - Every sandbox has to belong to a group.
// - Data can travel between sandboxes in the same group.
[[nodiscard]]
auto create_sandbox(id::group group, std::string_view sbox_exe_path) -> id::sandbox;

// Remove the given connection between two devices.
auto disconnect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in) -> void;

// Create a device by duplicating an existing device, and add it to the sandbox, synchronously.
// - This involves a round-trip to the sandbox process.
// - The target device can belong to a different sandbox.
// - The target device can belong to a different group.
[[nodiscard]]
auto duplicate(id::device dev, id::sandbox sbox) -> create_device_result;

// Duplicate a device asynchronously.
// - When the operation is complete, the callback will be called on the next call to ui_update(group).
// - Before the operation is complete, the returned device ID will be valid, but the device will be in an unloaded state.
[[nodiscard]]
auto duplicate_async(id::device dev, id::sandbox sbox, return_create_device_result fn) -> id::device;

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
[[nodiscard]]
auto find(id::device dev, ext::id::param param_id) -> idx::param;

// Find a scanned plugin with the given string ID.
// Returns < 0 if the plugin was not found.
[[nodiscard]]
auto find(ext::id::plugin plugin_id) -> id::plugin;

// Return a list of plugin files which failed to load.
[[nodiscard]]
auto get_broken_plugfiles() -> std::vector<id::plugfile>;

// Return a list of plugins which failed to load.
[[nodiscard]]
auto get_broken_plugins() -> std::vector<id::plugin>;

[[nodiscard]]
auto get_devices(id::sandbox sbox) -> std::vector<id::device>;

// If the device failed to load successfully, return the error string.
[[nodiscard]]
auto get_error(id::device dev) -> std::string_view;

// If the plugin file failed to scan, return the error string.
[[nodiscard]]
auto get_error(id::plugfile plugfile) -> std::string_view;

// If the plugin failed to load, return the error string.
[[nodiscard]]
auto get_error(id::plugin plugin) -> std::string_view;

// Returns the plugin ID string.
[[nodiscard]]
auto get_ext_id(id::plugin plugin) -> ext::id::plugin;

// Return device info.
[[nodiscard]]
auto get_info(id::device dev) -> device_info;

// Return the parameter info.
[[nodiscard]]
auto get_info(id::device dev, idx::param param) -> client_param_info;

// Returns the plugin name
[[nodiscard]]
auto get_name(id::plugin plugin) -> std::string_view;

// Return the file path of the plugin file.
[[nodiscard]]
auto get_path(id::plugfile plugfile) -> std::string_view;

// Return the plugin file for the given plugin.
[[nodiscard]]
auto get_plugfile(id::plugin plugin) -> id::plugfile;

// Get the current value of the parameter, synchronously.
//  - This involves a round-trip to the sandbox process.
[[nodiscard]]
auto get_value(id::device dev, idx::param param) -> double;

// Get the current value of the parameter, asynchronously.
//  - When the result is ready, call the given function with it on the next call to ui_update(group).
auto get_value_async(id::device dev, idx::param param, return_double fn) -> void;

// Returns the plugin vendor.
[[nodiscard]]
auto get_vendor(id::plugin plugin) -> std::string_view;

// Returns the plugin version string.
[[nodiscard]]
auto get_version(id::plugin plugin) -> std::string_view;

// For CLAP plugins, return the list of feature strings.
// Not sure how this will look for VST yet.
// This might change.
[[nodiscard]]
auto get_features(id::plugin plugin) -> std::vector<std::string>;

// Return the number of parameters for the given device.
[[nodiscard]]
auto get_param_count(id::device dev) -> size_t;

// Return the plugin for the given device.
[[nodiscard]]
auto get_plugin(id::device dev) -> id::plugin;

// Returns the plugin ID string for the given device.
[[nodiscard]]
auto get_plugin_ext_id(id::device dev) -> ext::id::plugin;

// Return the plugin type.
[[nodiscard]]
auto get_type(id::plugin plugin) -> plugin_type;

// Calculate the string representation of the given value, synchronously.
// - This involves a round-trip to the sandbox process.
[[nodiscard]]
auto get_value_text(id::device dev, idx::param param, double value) -> std::string;

// Calculate the string representation of the given value, asynchronously.
//  - When it is ready, call the given function with it, on the next call to ui_update(group).
auto get_value_text_async(id::device dev, idx::param param, double value, return_string fn) -> void;

// Return a list of plugins which at least appear to be working
// (they did not fail to load during plugin scanning.)
[[nodiscard]]
auto get_working_plugins() -> std::vector<id::plugin>;

// Hide the device editor window.
auto gui_hide(id::device dev) -> void;

// Show the device editor window.
auto gui_show(id::device dev) -> void;

// Return true if the device has a GUI.
[[nodiscard]]
auto has_gui(id::device dev) -> bool;

// Return true if the device has parameters.
[[nodiscard]]
auto has_params(id::device dev) -> bool;

// Return true if this plugin is suitable for use in a "rack", for example it is an audio
// effect or analyzer of some kind.
// For CLAP this is going to check the plugin features for "audio-effect" or "analyzer".
// I don't know what we will do for VSTs yet.
// I'm not sure I like this function so I might replace it with something else. Let me know
// what you think.
[[nodiscard]]
auto has_rack_features(id::plugin plugin) -> bool;

// Check if the given sandbox is running.
[[nodiscard]]
auto is_running(id::sandbox sbox) -> bool;

// Return true if the plugin scanner process is currently running.
[[nodiscard]]
auto is_scanning(void) -> bool;

// Load the device state and block until the operation is complete.
// Returns true if the device was loaded successfully.
[[nodiscard]]
auto load(id::device dev, const scuff::bytes& bytes) -> bool;

// Load the device state, asynchronously.
//  - When the operation is complete, call the given function,
//    on the next call to ui_update(group).
auto load_async(id::device dev, const scuff::bytes& bytes, return_load_device_result fn) -> void;

// Push a device event
auto push_event(id::device dev, const scuff::event& event) -> void;

// Restart the sandbox.
[[nodiscard]]
auto restart(id::sandbox sbox, std::string_view sbox_exe_path) -> bool;

// Save the device state.
[[nodiscard]]
auto save(id::device dev) -> scuff::bytes;

// Save the device state, asynchronously.
//  - When the operation is complete, call the given function with the result,
//    on the next call to ui_update(group).
auto save_async(id::device dev, return_bytes fn) -> void;

// Scan the system for plugins. If the scanner process is already
// running, it is restarted.
auto scan(std::string_view scan_exe_path, scan_flags flags) -> void;

// Set the render mode for the given group.
auto set_render_mode(id::group group, render_mode mode) -> void;

// Associate a track color with the device.
auto set_track_color(id::device dev, std::optional<rgba32> color) -> void;

// Associate a track name with the device.
auto set_track_name(id::device dev, std::string_view name) -> void;

// Return true if the device was created successfully.
[[nodiscard]]
auto was_created_successfully(id::device dev) -> bool;

} // scuff
