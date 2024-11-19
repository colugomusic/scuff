#pragma once

#include <cstdint>

namespace scuff {

static constexpr auto CHANNEL_COUNT         = 2;            // Hard-coded for now just to make things easier.
static constexpr auto CLAP_EXT              = ".clap";
static constexpr auto CLAP_SYMBOL_ENTRY     = "clap_entry";
static constexpr auto DIRTY_DEVICE_MS       = 1000;         // How often to save dirty device states.
static constexpr auto EVENT_PORT_SIZE       = 128;          // Max number of audio events per vector.
static constexpr auto GC_INTERVAL_MS        = 1000;
static constexpr auto HEARTBEAT_INTERVAL_MS = 1000;
static constexpr auto HEARTBEAT_TIMEOUT_MS  = 5000;
static constexpr auto INVALID_INDEX         = SIZE_MAX;
static constexpr auto MAX_AUDIO_PORTS       = 16;
static constexpr auto MAX_PARAMS            = 512;
static constexpr auto MSG_BUFFER_SIZE       = 4096;
static constexpr auto PARAM_ID_MAX          = 32;
static constexpr auto PARAM_NAME_MAX        = 64;
static constexpr auto POLL_SLEEP_MS         = 10;
static constexpr auto STACK_FN_CAPACITY     = 32;
static constexpr auto VECTOR_SIZE           = 64;          // Hard-coded for now just to make things easier.
static constexpr auto VST3_EXT              = ".vst3";

} // scuff
