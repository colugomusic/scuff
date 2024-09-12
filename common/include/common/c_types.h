#pragma once

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
