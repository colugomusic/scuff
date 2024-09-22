#pragma once

#include "c_plugin_type.h"
#include "c_types.h"

// This is almost just replicated from clap/events.h, since it is more or less a superset
// of VST3's events, and can be converted into VST3 events relatively easily.
//
// For documentation on this you can look at clap/events.h and find the
// corresponding struct in there.
//
// The reason for copying and renaming this from CLAP instead of simply including
// clap/events.h is:
//   1. To avoid the confusion of having to interact with VST3 plugins through a CLAP API.
//   2. To avoid the CLAP dependency in the scuff headers.
//   3. To give us the flexibility to change the event format if we need to.

typedef struct scuff_event_header_t {
	uint32_t size;
	uint32_t time;
	uint16_t CLAP_space_id;
	uint16_t event_type;
	uint16_t plugin_type;
	uint32_t flags;
} scuff_event_header;

enum {
	scuff_event_is_live     = 1 << 0,
	scuff_event_dont_record = 1 << 1,
};

enum {
	scuff_event_type_note_on             = 0,
	scuff_event_type_note_off            = 1,
	scuff_event_type_note_choke          = 2,
	scuff_event_type_note_end            = 3,
	scuff_event_type_note_expression     = 4,
	scuff_event_type_param_value         = 5,
	scuff_event_type_param_mod           = 6,
	scuff_event_type_param_gesture_begin = 7,
	scuff_event_type_param_gesture_end   = 8,
	scuff_event_type_transport           = 9,
	scuff_event_type_midi                = 10,
	scuff_event_type_midi_sysex          = 11,
	scuff_event_type_midi2               = 12,
};

typedef struct scuff_event_note_t {
	scuff_event_header_t header;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double  velocity;
} scuff_event_note;

enum {
	scuff_note_expression_volume     = 0,
	scuff_note_expression_pan        = 1,
	scuff_note_expression_tuning     = 2,
	scuff_note_expression_vibrato    = 3,
	scuff_note_expression_expression = 4,
	scuff_note_expression_brightness = 5,
	scuff_note_expression_pressure   = 6,
};

typedef int32_t scuff_note_expression;

typedef struct scuff_event_note_expression_t {
	scuff_event_header_t header;
	scuff_note_expression expression_id;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double value;
} scuff_event_note_expression;

typedef struct scuff_event_param_value_t {
	scuff_event_header_t header;
	void* cookie;      // For CLAP events
	uint32_t param_id; // Could be a clap_id, or a scuff_param, depending on the plugin type
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double value;
} scuff_event_param_value;

typedef struct scuff_event_param_mod_t {
	scuff_event_header_t header;
	void* cookie;      // For CLAP events
	uint32_t param_id; // Could be a clap_id, or a scuff_param, depending on the plugin type
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double amount;
} scuff_event_param_mod;

typedef struct scuff_event_param_gesture_t {
	scuff_event_header_t header;
	void* cookie;      // For CLAP events
	uint32_t param_id; // Could be a clap_id, or a scuff_param, depending on the plugin type
} scuff_event_param_gesture;

enum {
	scuff_transport_has_tempo            = 1 << 0,
	scuff_transport_has_beats_timeline   = 1 << 1,
	scuff_transport_has_seconds_timeline = 1 << 2,
	scuff_transport_has_time_signature   = 1 << 3,
	scuff_transport_is_playing           = 1 << 4,
	scuff_transport_is_recording         = 1 << 5,
	scuff_transport_is_loop_active       = 1 << 6,
	scuff_transport_is_within_pre_roll   = 1 << 7,
};

typedef int64_t scuff_beattime;
typedef int64_t scuff_sectime;

typedef struct scuff_event_transport_t {
	scuff_event_header_t header;
	uint32_t flags;
	scuff_beattime song_pos_beats;
	scuff_sectime  song_pos_seconds;
	double tempo;
	double tempo_inc;
	scuff_beattime loop_start_beats;
	scuff_beattime loop_end_beats;
	scuff_sectime  loop_start_seconds;
	scuff_sectime  loop_end_seconds;
	scuff_beattime bar_start;
	int32_t       bar_number;
	uint16_t tsig_num;
	uint16_t tsig_denom;
} scuff_event_transport;

typedef struct scuff_event_midi_t {
	scuff_event_header_t header;
	uint16_t port_index;
	uint8_t  data[3];
} scuff_event_midi;

typedef struct scuff_event_midi_sysex_t {
	scuff_event_header_t header;
	uint16_t       port_index;
	const uint8_t *buffer;
	uint32_t       size;
} scuff_event_midi_sysex;

typedef struct scuff_event_midi2_t {
	scuff_event_header_t header;
	uint16_t port_index;
	uint32_t data[4];
} scuff_event_midi2;
