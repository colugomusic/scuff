#pragma once

#include "common/constants.hpp"
#include "common/events.hpp"
#include "common/inplace_function.hpp"
#include "common/param_info.hpp"
#include "common/plugin_type.hpp"
#include "common/render_mode.hpp"
#include "common/types.hpp"
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

// Users should assume that any of these callbacks can be called from any non-realtime thread.
// Every callback must be provided when calling scuff::init.
using on_device_error          = std::function<void(id::device dev, const char* error)>;
using on_device_params_changed = std::function<void(id::device dev)>;
using on_error                 = std::function<void(const char* error)>;
using on_plugfile_broken       = std::function<void(id::plugfile plugfile)>;
using on_plugfile_scanned      = std::function<void(id::plugfile plugfile)>;
using on_plugin_broken         = std::function<void(id::plugin plugin)>;
using on_plugin_scanned        = std::function<void(id::plugin plugin)>;
using on_sbox_crashed          = std::function<void(id::sandbox sbox, const char* error)>;
using on_sbox_error            = std::function<void(id::sandbox sbox, const char* error)>;
using on_sbox_info             = std::function<void(id::sandbox sbox, const char* info)>;
using on_sbox_started          = std::function<void(id::sandbox sbox)>;
using on_sbox_warning          = std::function<void(id::sandbox sbox, const char* warning)>;
using on_scan_complete         = std::function<void()>;
using on_scan_error            = std::function<void(const char* error)>;
using on_scan_started          = std::function<void()>;
using return_bytes             = std::function<void(const std::vector<std::byte>& bytes)>;
using return_device            = std::function<void(id::device dev, bool success)>;
using return_double            = std::function<void(double value)>;
using return_string            = std::function<void(const char* text)>;


struct callbacks {
	on_device_error on_device_error;
	on_device_params_changed on_device_params_changed;
	on_error on_error;
	on_plugfile_broken on_plugfile_broken;
	on_plugfile_scanned on_plugfile_scanned;
	on_plugin_broken on_plugin_broken;
	on_plugin_scanned on_plugin_scanned;
	on_sbox_crashed on_sbox_crashed;
	on_sbox_error on_sbox_error;
	on_sbox_info on_sbox_info;
	on_sbox_started on_sbox_started;
	on_sbox_warning on_sbox_warning;
	on_scan_complete on_scan_complete;
	on_scan_error on_scan_error;
	on_scan_started on_scan_started;
};

struct config {
	scuff::callbacks callbacks;
};

/////////////////////////////////////////////////////////////////////////////////////////
// Audio thread
/////////////////////////////////////////////////////////////////////////////////////////

// Process the sandbox group. This is safe to call in a realtime thread.
void             audio_process(group_process process);

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
bool             init(const scuff::config* config);

// Call this when you're done with the sandboxing system.
//  - Don't call anything else after this.
void             shutdown(void);

// Close all editor windows.
void             close_all_editors(void);

// Connect the audio output of one device to the audio input of another device.
//  - The devices don't have to belong to the same sandbox - the connections are allowed
//    to cross from one sandbox to another, within the same sandbox group.
void             connect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in);

// Create a device and add it to the sandbox asynchronously.
//  - When the operation is complete, call the given function with the device handle.
//  - If the device fails to load, it will still be created, but it will be in an error
//    state.
//  - You can create a device with a plugin ID that hasn't been scanned yet. It will be
//    created in an error state and will remain that way until the plugin is found by
//    a future scan where the reload_failed_devices flag is set.
void             create_device(id::sandbox sbox, plugin_type type, ext::id::plugin plugin_id, return_device fn);

// Remove the given connection between two devices.
void             disconnect(id::device dev_out, size_t port_out, id::device dev_in, size_t port_in);

// Create a device by duplicating an existing device, and add it to the sandbox,
// asynchronously.
// When the operation is complete, call the given function with the device handle.
// - The target device can belong to a different sandbox.
void             duplicate(id::device dev, id::sandbox sbox, return_device fn);

// Erase a device.
// It's OK to do this while the audio thread is processing. The device will be
// erased when it's safe to do so.
void             erase(id::device dev);

// Hide the device editor window.
void             gui_hide(id::device dev); 

// Show the device editor window.
void             gui_show(id::device dev);

// If the device failed to load successfully, return the error string.
const char*      get_error(id::device dev);

// Return the device name.
const char*      get_name(id::device dev);

// Return the number of parameters for the given device.
size_t           get_param_count(id::device dev);

// Calculate the string representation of the given value asynchronously.
// When it is ready, call the given function with it.
void             get_param_value_text(id::device dev, idx::param param, double value, return_string fn);

// Return the plugin for the given device.
id::plugin       get_plugin(id::device dev);

// Return true if the device has a GUI.
bool             has_gui(id::device dev);

// Return true if the device has parameters.
bool             has_params(id::device dev);

// Load the device state asynchronously.
void             load(id::device dev, const void* bytes, size_t count);

// Save the device state asynchronously.
void             save(id::device dev, return_bytes fn);

// Set the render mode for the given device.
void             set_render_mode(id::device dev, render_mode mode);

// Return true if the device loaded successfully.
bool             was_loaded_successfully(id::device dev);

// Activate audio processing for the sandbox group.
void             activate(id::group group, double sr);

// Deactivate audio processing for the sandbox group.
void             deactivate(id::group group);

// Create a new group.
// - Every sandbox has to belong to a group.
// - This is what allows data to travel between sandboxes.
// - On failure, returns < 0.
id::group        create_group(void);

// Erase a group.
// It's OK to do this while the audio thread is processing. The group will be
// erased when it's safe to do so.
void             erase(id::group group);

// Create a new sandbox.
// - Every sandbox has to belong to a group.
// - Data can travel between sandboxes in the same group.
// - If starting the sandbox process fails, the sandbox will still be created,
//   but it will be in an error state.
id::sandbox      create_sandbox(id::group group, const char* sbox_exe_path);

// Erase a sandbox.
// It's OK to do this while the audio thread is processing. The sandbox will be
// erased when it's safe to do so.
void             erase(id::sandbox sbox);

// If the sandbox failed to start, return the error string.
const char*      get_error(id::sandbox sbox);

// Check if the given sandbox is running.
bool             is_running(id::sandbox sbox);

// Return true if the plugin scanner process is currently running.
bool             is_scanning(void);

// Find the device parameter with the given id.
// The id is either a Steinberg::Vst::ParamID or a clap_id.
// - This will return an invalid index if the device hasn't finished being created yet.
idx::param       find(id::device dev, ext::id::param param_id);

// Return the parameter info.
param_info       get_info(id::device dev, idx::param param);

// Get the current value of the parameter, asynchronously.
// When the result is ready, call the given function with it.
void             get_value(id::device dev, idx::param param, return_double fn);

// If the plugin file failed to scan, return the error string.
const char*      get_error(id::plugfile plugfile);

// Return the file path of the plugin file.
const char*      get_path(id::plugfile plugfile);

// Find a scanned plugin with the given string ID.
// Returns < 0 if the plugin was not found.
id::plugin       find(ext::id::plugin plugin_id);

// If the plugin failed to load, return the error string.
const char*      get_error(id::plugin plugin);

// Returns the plugin ID string.
ext::id::plugin  get_ext_id(id::plugin plugin);

// Returns the plugin name
const char*      get_name(id::plugin plugin);

// Returns the plugin vendor.
const char*      get_vendor(id::plugin plugin);

// Returns the plugin version string.
const char*      get_version(id::plugin plugin);

// Push a device event
void             push_event(id::device dev, const scuff::event& event);

// Restart the sandbox.
void             restart(id::sandbox sbox, const char* sbox_exe_path);

// Scan the system for plugins. If the scanner process is already
// running, it is restarted.
void             scan(const char* scan_exe_path, int flags);

} // scuff
