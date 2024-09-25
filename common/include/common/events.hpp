#pragma once

#include "constants.hpp"
#include "types.hpp"
#include <variant>

namespace scuff::events {

// This is almost just replicated from clap/events.h, since it is more or less a superset
// of VST3's events, and can be converted into VST3 events relatively easily.
//
// For documentation on this you can look at clap/events.h and find the
// corresponding struct in there.
//
// The reason for copying all this from CLAP instead of simply including clap/events.h is:
//   1. To avoid the confusion of having to interact with VST3 plugins through a CLAP API.
//   2. To avoid the CLAP dependency in the scuff headers.
//   3. To give us the flexibility to change the event format if we need to.

enum flags {
	flags_is_live     = 1 << 0,
	flags_dont_record = 1 << 1,
};

enum class type {
	note_on             = 0,
	note_off            = 1,
	note_choke          = 2,
	note_end            = 3,
	note_expression     = 4,
	param_value         = 5,
	param_mod           = 6,
	param_gesture_begin = 7,
	param_gesture_end   = 8,
	transport           = 9,
	midi                = 10,
	midi_sysex          = 11,
	midi2               = 12,
};

struct header {
	uint32_t size;
	uint32_t time;
	uint16_t CLAP_space_id;
	events::type event_type;
	events::flags flags;
};

struct note {
	events::header header;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double  velocity;
};

enum class note_expression_id {
	volume     = 0,
	pan        = 1,
	tuning     = 2,
	vibrato    = 3,
	expression = 4,
	brightness = 5,
	pressure   = 6,
};

struct note_expression {
	events::header header;
	note_expression_id id;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double value;
};

struct param_value {
	events::header header;
	size_t param;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double value;
};

struct param_mod {
	events::header header;
	size_t param;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double amount;
};

struct param_gesture {
	events::header header;
	size_t param;
};

enum transport_flags {
	transport_flags_has_tempo            = 1 << 0,
	transport_flags_has_beats_timeline   = 1 << 1,
	transport_flags_has_seconds_timeline = 1 << 2,
	transport_flags_has_time_signature   = 1 << 3,
	transport_flags_is_playing           = 1 << 4,
	transport_flags_is_recording         = 1 << 5,
	transport_flags_is_loop_active       = 1 << 6,
	transport_flags_is_within_pre_roll   = 1 << 7,
};

using beattime = int64_t;
using sectime  = int64_t;

struct transport {
	events::header header;
	uint32_t flags;
	beattime song_pos_beats;
	sectime  song_pos_seconds;
	double   tempo;
	double   tempo_inc;
	beattime loop_start_beats;
	beattime loop_end_beats;
	sectime  loop_start_seconds;
	sectime  loop_end_seconds;
	beattime bar_start;
	int32_t  bar_number;
	uint16_t tsig_num;
	uint16_t tsig_denom;
};

struct midi {
	events::header header;
	uint16_t port_index;
	uint8_t  data[3];
};

struct midi_sysex {
	events::header header;
	uint16_t       port_index;
	const uint8_t *buffer;
	uint32_t       size;
};

struct midi2 {
	events::header header;
	uint16_t port_index;
	uint32_t data[4];
};

} // scuff::events

namespace scuff {

using event = std::variant<
	events::midi_sysex,
	events::midi,
	events::midi2,
	events::note_expression,
	events::param_gesture,
	events::param_mod,
	events::param_value,
	events::transport
>;

} // scuff
