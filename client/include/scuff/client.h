#pragma once

#include "../../../common/c_constants.h"
#include "../../../common/c_events.h"
#include "../../../common/c_render_mode.h"
#include "../../../common/c_string_options.h"
#include <stdint.h>

typedef double      scuff_sample_rate;

// This refers to a .clap or .vst3 file, or a VST2 shared library.
typedef int64_t     scuff_plugfile;

// This refers to an instance of a plugin.
typedef int64_t     scuff_device;

// This refers to a group of sandboxes. Sandboxes all belong to a group.
typedef int64_t     scuff_group;

// This refers to a plugin.
typedef int64_t     scuff_plugin;

// This refers to a sandbox.
typedef int64_t     scuff_sbox;

// A device parameter index.
typedef size_t      scuff_param;

// A plugin parameter string id.
typedef const char* scuff_param_id;

// A plugin string id.
typedef const char* scuff_plugin_id;

enum scuff_plugin_type { clap, vst };

typedef struct scuff_audio_writer_t {
	void* ctx;
	size_t port_index;
	void (*fn)(const struct scuff_audio_writer_t* ctx, float* floats);
} scuff_audio_writer;

typedef struct scuff_audio_reader_t {
	void* ctx;
	size_t port_index;
	void (*fn)(const struct scuff_audio_reader_t* ctx, const float* floats);
} scuff_audio_reader;

typedef struct scuff_event_writer_t {
	void* ctx;
	size_t (*count)(const struct scuff_event_writer_t* ctx);
	const scuff_event_header* (*get)(const struct scuff_event_writer_t* ctx, size_t index);
} scuff_event_writer;

typedef struct scuff_event_reader_t {
	void* ctx;
	void (*push)(const struct scuff_event_reader_t* ctx, const scuff_event_header* event);
} scuff_event_reader;

typedef struct scuff_audio_writers_t  { size_t count; const scuff_audio_writer* writers; } scuff_audio_writers; 
typedef struct scuff_audio_readers_t  { size_t count; const scuff_audio_reader* readers; } scuff_audio_readers; 
typedef struct scuff_input_device_t   { scuff_device dev; scuff_audio_writers audio_writers; scuff_event_writer event_writer; } scuff_input_device;
typedef struct scuff_output_device_t  { scuff_device dev; scuff_audio_readers audio_readers; scuff_event_reader event_reader; } scuff_output_device;
typedef struct scuff_input_devices_t  { size_t count; const scuff_input_device* devices; } scuff_input_devices;
typedef struct scuff_output_devices_t { size_t count; const scuff_output_device* devices; } scuff_output_devices;

typedef struct scuff_group_process_t {
	scuff_group group;
	scuff_input_devices input_devices;
	scuff_output_devices output_devices;
} scuff_group_process;

typedef struct scuff_on_plugfile_scanned_t { void* ctx; void (*fn)(const struct scuff_on_plugfile_scanned_t* ctx, scuff_plugfile plugfile); } scuff_on_plugfile_scanned;
typedef struct scuff_on_plugin_scanned_t   { void* ctx; void (*fn)(const struct scuff_on_plugin_scanned_t* ctx, scuff_plugin plugin); } scuff_on_plugin_scanned;
typedef struct scuff_on_sbox_crashed_t     { void* ctx; void (*fn)(const struct scuff_on_sbox_crashed_t* ctx, scuff_sbox sbox); } scuff_on_sbox_crashed;
typedef struct scuff_on_sbox_started_t     { void* ctx; void (*fn)(const struct scuff_on_sbox_started_t* ctx, scuff_sbox sbox); } scuff_on_sbox_started;
typedef struct scuff_on_scan_complete_t    { void* ctx; void (*fn)(const struct scuff_on_scan_complete_t* ctx); } scuff_on_scan_complete;
typedef struct scuff_return_device_t       { void* ctx; void (*fn)(const struct scuff_return_device_t* ctx, scuff_device dev); } scuff_return_device; 
typedef struct scuff_return_double_t       { void* ctx; void (*fn)(const struct scuff_return_double_t* ctx, double value); } scuff_return_double;
typedef struct scuff_return_param_t        { void* ctx; void (*fn)(const struct scuff_return_param_t* ctx, scuff_param param); } scuff_return_param;
typedef struct scuff_return_string_t       { void* ctx; void (*fn)(const struct scuff_return_string_t* ctx, const char* text); } scuff_return_string; 

typedef struct scuff_callbacks_t {
	scuff_on_plugfile_scanned on_plugfile_scanned;
	scuff_on_plugin_scanned on_plugin_scanned;
	scuff_on_sbox_crashed on_sbox_crashed;
	scuff_on_sbox_started on_sbox_started;
	scuff_on_scan_complete on_scan_complete;
} scuff_callbacks;

typedef struct scuff_config_t {
	scuff_callbacks callbacks;
	scuff_string_options string_options;
} scuff_config;

extern "C" {

/////////////////////////////////////////////////////////////////////////////////////////
// Audio thread
/////////////////////////////////////////////////////////////////////////////////////////

// Process the sandbox group. This is safe to call in a realtime thread.
void            scuff_audio_process(scuff_group_process process);

/////////////////////////////////////////////////////////////////////////////////////////
// The rest of these functions are thread-safe, but NOT necessarily realtime-safe.
// 
// Some of these are asynchronous, so instead of returning a value, you have to provide a
// function for them to call when they're done. These functions are written this way
// because they need to send a message to the sandbox process to do their work.
/////////////////////////////////////////////////////////////////////////////////////////

// Call this before anything else.
void            scuff_init(const scuff_config* config);

// Call this when you're done with the sandboxing system.
// Don't call anything else after this.
void            scuff_shutdown();

// Close all editor windows.
void            scuff_close_all_editors();

// Connect the audio output of one device to the audio input of another device.
// The devices don't have to belong to the same sandbox - the connections are allowed to
// cross from one sandbox to another, within the same sandbox group.
void            scuff_device_connect(scuff_device dev_out, size_t port_out, scuff_device dev_in, size_t port_in);

// Create a device and add it to the sandbox asynchronously.
// When the operation is complete, call the given function with the device handle.
void            scuff_device_create(scuff_sbox sbox, scuff_plugin plugin, scuff_return_device fn);

// Remove the given connection between two devices.
void            scuff_device_disconnect(scuff_device dev_out, size_t port_out, scuff_device dev_in, size_t port_in);

// Create a device by duplicating an existing device, and add it to the sandbox,
// asynchronously.
// When the operation is complete, call the given function with the device handle.
// - The target device can belong to a different sandbox.
void            scuff_device_duplicate(scuff_device dev, scuff_sbox sbox, scuff_return_device fn);

// Erase a device.
// It's OK to do this while the audio thread is processing. The device will be
// erased when it's safe to do so.
void            scuff_device_erase(scuff_device dev);

// Hide the device editor window.
void            scuff_device_gui_hide(scuff_device dev); 

// Show the device editor window.
void            scuff_device_gui_show(scuff_device dev);

// If the device failed to load successfully, return the error string.
const char*     scuff_device_get_error(scuff_device dev);

// Set the render mode for the given device.
void            scuff_device_set_render_mode(scuff_device dev, scuff_render_mode mode);

// Return the device name.
const char*     scuff_device_get_name(scuff_device dev);

// Return the number of parameters for the given device.
size_t          scuff_device_get_param_count(scuff_device dev);

// Calculate the string representation of the given value asynchronously.
// When it is ready, call the given function with it.
void            scuff_device_get_param_value_text(scuff_device dev, scuff_param param, double value, scuff_return_string fn);

// Return the plugin for the given device.
scuff_plugin    scuff_device_get_plugin(scuff_device dev);

// Return true if the device has a GUI.
bool            scuff_device_has_gui(scuff_device dev);

// Return true if the device has parameters.
bool            scuff_device_has_params(scuff_device dev);

// Return true if the device loaded successfully.
bool            scuff_device_was_loaded_successfully(scuff_device dev);

// Create a new group.
// - Every sandbox has to belong to a group.
// - This is what allows data to travel between sandboxes.
scuff_group     scuff_group_create();

// Erase a group.
// It's OK to do this while the audio thread is processing. The group will be
// erased when it's safe to do so.
void            scuff_group_erase(scuff_group group);

// Create a new sandbox.
// - Every sandbox has to belong to a group.
// - Data can travel between sandboxes in the same group.
scuff_sbox      scuff_sandbox_create(scuff_group group);

// Erase a sandbox.
// It's OK to do this while the audio thread is processing. The sandbox will be
// erased when it's safe to do so.
void            scuff_sandbox_erase(scuff_sbox sbox);

// If the sandbox failed to start, return the error string.
const char*     scuff_sandbox_get_error(scuff_sbox sbox);

// Check if the given sandbox is running.
bool            scuff_is_running(scuff_sbox sbox);

// Return true if the plugin scanner process is currently running.
bool            scuff_is_scanning();

// Find the parameter with the given id asynchronously.
// When the result is ready, call the given function with it.
// If the parameter was not found then the result will be TOM_INVALID_INDEX.
void            scuff_param_find(scuff_device dev, scuff_param_id param_id, scuff_return_param fn);

// Begin a parameter gesture (CLAP)
void            scuff_param_gesture_begin(scuff_device dev, scuff_param param);

// End a parameter gesture (CLAP)
void            scuff_param_gesture_end(scuff_device dev, scuff_param param);

// Set a parameter value.
void            scuff_param_set_value(scuff_device dev, scuff_param param, double value);

// Get the current value of the parameter, asynchronously.
// When the result is ready, call the given function with it.
void            scuff_param_get_value(scuff_device dev, scuff_param param, scuff_return_double fn);

// If the plugin file failed to scan, return the error string.
const char*     scuff_plugfile_get_error(scuff_plugfile plugfile);

// Return the file path of the plugin file.
const char*     scuff_plugfile_get_path(scuff_plugfile plugfile);

// If the plugin failed to load, return the error string.
const char*     scuff_plugin_get_error(scuff_plugin plugin);

// Returns the plugin ID string.
scuff_plugin_id scuff_plugin_get_id(scuff_plugin plugin);

// Returns the plugin name
const char*     scuff_plugin_get_name(scuff_plugin plugin);

// Returns the plugin vendor.
const char*     scuff_plugin_get_vendor(scuff_plugin plugin);

// Returns the plugin version string.
const char*     scuff_plugin_get_version(scuff_plugin plugin);

// Restart the sandbox.
void            scuff_restart(scuff_sbox sbox);

// Scan the system for plugins. If the scanner process is already
// running, it is restarted.
void            scuff_scan();

// Find a scanned plugin with the given string ID.
scuff_plugin    scuff_plugin_find(scuff_plugin_id plugin_id);

// Set the sample rate for all devices.
void            scuff_set_sample_rate(scuff_sample_rate sr);

} // extern "C"
