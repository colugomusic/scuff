#pragma once

#include "c_constants.h"
#include "c_events.h"
#include "serialization.hpp"
#include "visit.hpp"
#include <boost/container/static_vector.hpp>
#include <clap/events.h>
#include <string_view>
#include <variant>

namespace bc = boost::container;

namespace scuff::events {

using event = std::variant<
	scuff_event_midi_sysex,
	scuff_event_midi,
	scuff_event_midi2,
	scuff_event_note_expression,
	scuff_event_param_gesture,
	scuff_event_param_mod,
	scuff_event_param_value,
	scuff_event_transport
>;

[[nodiscard]] static
auto to_event(const scuff_event_header& e) -> event {
	if (e.type == scuff_event_type_midi_sysex)          { return *reinterpret_cast<const scuff_event_midi_sysex*>(&e); }
	if (e.type == scuff_event_type_midi)                { return *reinterpret_cast<const scuff_event_midi*>(&e); }
	if (e.type == scuff_event_type_midi2)               { return *reinterpret_cast<const scuff_event_midi2*>(&e); }
	if (e.type == scuff_event_type_note_expression)     { return *reinterpret_cast<const scuff_event_note_expression*>(&e); }
	if (e.type == scuff_event_type_param_gesture_begin) { return *reinterpret_cast<const scuff_event_param_gesture*>(&e); }
	if (e.type == scuff_event_type_param_gesture_end)   { return *reinterpret_cast<const scuff_event_param_gesture*>(&e); }
	if (e.type == scuff_event_type_param_mod)           { return *reinterpret_cast<const scuff_event_param_mod*>(&e); }
	if (e.type == scuff_event_type_param_value)         { return *reinterpret_cast<const scuff_event_param_value*>(&e); }
	if (e.type == scuff_event_type_transport)           { return *reinterpret_cast<const scuff_event_transport*>(&e); }
	throw std::runtime_error("scuff::events::convert(event): Invalid event type");
}

[[nodiscard]] static
auto to_header(const event& e) -> const scuff_event_header& {
	if (std::holds_alternative<scuff_event_midi_sysex>(e))      { return std::get<scuff_event_midi_sysex>(e).header; }
	if (std::holds_alternative<scuff_event_midi>(e))            { return std::get<scuff_event_midi>(e).header; }
	if (std::holds_alternative<scuff_event_midi2>(e))           { return std::get<scuff_event_midi2>(e).header; }
	if (std::holds_alternative<scuff_event_note_expression>(e)) { return std::get<scuff_event_note_expression>(e).header; }
	if (std::holds_alternative<scuff_event_param_gesture>(e))   { return std::get<scuff_event_param_gesture>(e).header; }
	if (std::holds_alternative<scuff_event_param_mod>(e))       { return std::get<scuff_event_param_mod>(e).header; }
	if (std::holds_alternative<scuff_event_param_value>(e))     { return std::get<scuff_event_param_value>(e).header; }
	if (std::holds_alternative<scuff_event_transport>(e))       { return std::get<scuff_event_transport>(e).header; }
	throw std::runtime_error("scuff::events::convert(event): Invalid event");
}

using event_buffer = bc::static_vector<event, SCUFF_EVENT_PORT_SIZE>;

} // scuff::events

namespace scuff::events::clap {

using event = std::variant<
	clap_event_midi_sysex_t,
	clap_event_midi_t,
	clap_event_midi2_t,
	clap_event_note_expression_t,
	clap_event_param_gesture_t,
	clap_event_param_mod_t,
	clap_event_param_value_t,
	clap_event_transport_t
>;

template <typename Fn> concept find_param_fn           = requires(Fn fn, void* cookie, clap_id param_id) { { fn(cookie, param_id) } -> std::same_as<scuff_param>; };
template <typename Fn> concept get_param_cookie_fn     = requires(Fn fn, scuff_param param) { { fn(param) } -> std::same_as<void*>; };
template <typename Fn> concept get_param_id_fn         = requires(Fn fn, scuff_param param) { { fn(param) } -> std::same_as<clap_id>; };
template <typename T>  concept has_find_param_fn       = requires { { T::find_param } -> find_param_fn; };
template <typename T>  concept has_get_param_cookie_fn = requires { { T::get_param_cookie } -> get_param_cookie_fn; };
template <typename T>  concept has_get_param_id_fn     = requires { { T::get_param_id } -> get_param_id_fn; };

template <typename T> concept clap_to_scuff_conversion = has_find_param_fn<T>;
template <typename T> concept scuff_to_clap_conversion = has_get_param_cookie_fn<T> && has_get_param_id_fn<T>;

template <scuff::events::clap::find_param_fn FindParamFn>
struct clap_to_scuff_conversion_fns {
	FindParamFn find_param;
};

template <scuff::events::clap::get_param_cookie_fn GetParamCookieFn,
	      scuff::events::clap::get_param_id_fn GetParamIdFn>
struct scuff_to_clap_conversion_fns {
	GetParamCookieFn get_param_cookie;
	GetParamIdFn     get_param_id;
};

[[nodiscard]] static
auto to_header(const event& e) -> const clap_event_header_t& {
	if (std::holds_alternative<clap_event_midi_sysex_t>(e))      { return std::get<clap_event_midi_sysex_t>(e).header; }
	if (std::holds_alternative<clap_event_midi_t>(e))            { return std::get<clap_event_midi_t>(e).header; }
	if (std::holds_alternative<clap_event_midi2_t>(e))           { return std::get<clap_event_midi2_t>(e).header; }
	if (std::holds_alternative<clap_event_note_expression_t>(e)) { return std::get<clap_event_note_expression_t>(e).header; }
	if (std::holds_alternative<clap_event_param_gesture_t>(e))   { return std::get<clap_event_param_gesture_t>(e).header; }
	if (std::holds_alternative<clap_event_param_mod_t>(e))       { return std::get<clap_event_param_mod_t>(e).header; }
	if (std::holds_alternative<clap_event_param_value_t>(e))     { return std::get<clap_event_param_value_t>(e).header; }
	if (std::holds_alternative<clap_event_transport_t>(e))       { return std::get<clap_event_transport_t>(e).header; }
	throw std::runtime_error("scuff::events::clap::convert(event): Invalid event");
}

[[nodiscard]] static
auto to_event(const clap_event_header& e) -> clap::event {
	if (e.type == CLAP_EVENT_MIDI_SYSEX)          { return *reinterpret_cast<const clap_event_midi_sysex_t*>(&e); }
	if (e.type == CLAP_EVENT_MIDI)                { return *reinterpret_cast<const clap_event_midi_t*>(&e); }
	if (e.type == CLAP_EVENT_MIDI2)               { return *reinterpret_cast<const clap_event_midi2_t*>(&e); }
	if (e.type == CLAP_EVENT_NOTE_EXPRESSION)     { return *reinterpret_cast<const clap_event_note_expression_t*>(&e); }
	if (e.type == CLAP_EVENT_PARAM_GESTURE_BEGIN) { return *reinterpret_cast<const clap_event_param_gesture_t*>(&e); }
	if (e.type == CLAP_EVENT_PARAM_GESTURE_END)   { return *reinterpret_cast<const clap_event_param_gesture_t*>(&e); }
	if (e.type == CLAP_EVENT_PARAM_MOD)           { return *reinterpret_cast<const clap_event_param_mod_t*>(&e); }
	if (e.type == CLAP_EVENT_PARAM_VALUE)         { return *reinterpret_cast<const clap_event_param_value_t*>(&e); }
	if (e.type == CLAP_EVENT_TRANSPORT)           { return *reinterpret_cast<const clap_event_transport_t*>(&e); }
	throw std::runtime_error("scuff::events::clap::convert(event): Invalid event type");
}

[[nodiscard]] static
auto flags_from_scuff(uint32_t flags) -> uint32_t {
	uint32_t out = 0;
	out |= (flags & scuff_event_is_live)     ? CLAP_EVENT_IS_LIVE     : 0;
	out |= (flags & scuff_event_dont_record) ? CLAP_EVENT_DONT_RECORD : 0;
	return out;
}

[[nodiscard]] static
auto transport_flags_from_scuff(uint32_t flags) -> uint32_t {
	uint32_t out = 0;
	out |= (flags & scuff_transport_has_beats_timeline)   ? CLAP_TRANSPORT_HAS_BEATS_TIMELINE   : 0;
	out |= (flags & scuff_transport_has_tempo)            ? CLAP_TRANSPORT_HAS_TEMPO            : 0;
	out |= (flags & scuff_transport_has_time_signature)   ? CLAP_TRANSPORT_HAS_TIME_SIGNATURE   : 0;
	out |= (flags & scuff_transport_has_seconds_timeline) ? CLAP_TRANSPORT_HAS_SECONDS_TIMELINE : 0;
	out |= (flags & scuff_transport_is_loop_active)       ? CLAP_TRANSPORT_IS_LOOP_ACTIVE       : 0;
	out |= (flags & scuff_transport_is_playing)           ? CLAP_TRANSPORT_IS_PLAYING           : 0;
	out |= (flags & scuff_transport_is_recording)         ? CLAP_TRANSPORT_IS_RECORDING         : 0;
	out |= (flags & scuff_transport_is_within_pre_roll)   ? CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL   : 0;
	return out;
}

[[nodiscard]] static
auto type_from_scuff(uint16_t type) -> uint16_t {
	switch (type) {
		case scuff_event_type_midi:                return CLAP_EVENT_MIDI;
		case scuff_event_type_midi_sysex:          return CLAP_EVENT_MIDI_SYSEX;
		case scuff_event_type_midi2:               return CLAP_EVENT_MIDI2;
		case scuff_event_type_note_expression:     return CLAP_EVENT_NOTE_EXPRESSION;
		case scuff_event_type_param_gesture_begin: return CLAP_EVENT_PARAM_GESTURE_BEGIN;
		case scuff_event_type_param_gesture_end:   return CLAP_EVENT_PARAM_GESTURE_END;
		case scuff_event_type_param_mod:           return CLAP_EVENT_PARAM_MOD;
		case scuff_event_type_param_value:         return CLAP_EVENT_PARAM_VALUE;
		case scuff_event_type_transport:           return CLAP_EVENT_TRANSPORT;
		default: throw std::runtime_error("scuff::events::clap::type_from_scuff: Invalid event type");
	}
}

[[nodiscard]] static
auto from_scuff(const scuff_event_header& hdr) -> clap_event_header_t {
	clap_event_header_t out;
	out.space_id = hdr.CLAP_space_id;
	out.flags    = flags_from_scuff(hdr.flags);
	out.size     = hdr.size;
	out.time     = hdr.time;
	out.type     = type_from_scuff(hdr.type);
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff_(const scuff_event_midi_sysex& e, const Conv& fns) -> event {
	clap_event_midi_sysex_t out;
	out.header     = from_scuff(e.header);
	out.port_index = e.port_index;
	out.size       = e.size;
	out.buffer     = e.buffer;
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff_(const scuff_event_midi& e, const Conv& fns) -> event {
	clap_event_midi_t out;
	out.header     = from_scuff(e.header);
	out.port_index = e.port_index;
	std::copy(std::begin(e.data), std::end(e.data), std::begin(out.data));
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff_(const scuff_event_midi2& e, const Conv& fns) -> event {
	clap_event_midi2_t out;
	out.header     = from_scuff(e.header);
	out.port_index = e.port_index;
	std::copy(std::begin(e.data), std::end(e.data), std::begin(out.data));
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff_(const scuff_event_note_expression& e, const Conv& fns) -> event {
	clap_event_note_expression_t out;
	out.expression_id = e.expression_id;
	out.header        = from_scuff(e.header);
	out.key           = e.key;
	out.note_id       = e.note_id;
	out.port_index    = e.port_index;
	out.value         = e.value;
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff_(const scuff_event_param_gesture& e, const Conv& fns) -> event {
	clap_event_param_gesture_t out;
	out.header   = from_scuff(e.header);
	out.param_id = fns.get_param_id(e.param);
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff_(const scuff_event_param_mod& e, const Conv& fns) -> event {
	clap_event_param_mod_t out;
	out.amount     = e.amount;
	out.channel    = e.channel;
	out.header     = from_scuff(e.header);
	out.key        = e.key;
	out.note_id    = e.note_id;
	out.port_index = e.port_index;
	out.param_id   = fns.get_param_id(e.param);
	out.cookie     = fns.get_param_cookie(e.param);
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff_(const scuff_event_param_value& e, const Conv& fns) -> event {
	clap_event_param_value_t out;
	out.channel    = e.channel;
	out.header     = from_scuff(e.header);
	out.key        = e.key;
	out.note_id    = e.note_id;
	out.port_index = e.port_index;
	out.value      = e.value;
	out.param_id   = fns.get_param_id(e.param);
	out.cookie     = fns.get_param_cookie(e.param);
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff_(const scuff_event_transport& e, const Conv& fns) -> event {
	clap_event_transport_t out;
	out.bar_number         = e.bar_number;
	out.bar_start          = e.bar_start;
	out.flags              = transport_flags_from_scuff(e.flags);
	out.header             = from_scuff(e.header);
	out.loop_end_beats     = e.loop_end_beats;
	out.loop_end_seconds   = e.loop_end_seconds;
	out.loop_start_beats   = e.loop_start_beats;
	out.loop_start_seconds = e.loop_start_seconds;
	out.song_pos_beats     = e.song_pos_beats;
	out.song_pos_seconds   = e.song_pos_seconds;
	out.tempo              = e.tempo;
	out.tempo_inc          = e.tempo_inc;
	out.tsig_denom         = e.tsig_denom;
	out.tsig_num           = e.tsig_num;
	return out;
}

[[nodiscard]] static
auto flags_to_scuff(uint32_t flags) -> uint32_t {
	uint32_t out = 0;
	out |= (flags & CLAP_EVENT_IS_LIVE)     ? scuff_event_is_live     : 0;
	out |= (flags & CLAP_EVENT_DONT_RECORD) ? scuff_event_dont_record : 0;
	return out;
}

[[nodiscard]] static
auto transport_flags_to_scuff(uint32_t flags) -> uint32_t {
	uint32_t out = 0;
	out |= (flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)   ? scuff_transport_has_beats_timeline   : 0;
	out |= (flags & CLAP_TRANSPORT_HAS_TEMPO)            ? scuff_transport_has_tempo            : 0;
	out |= (flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE)   ? scuff_transport_has_time_signature   : 0;
	out |= (flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) ? scuff_transport_has_seconds_timeline : 0;
	out |= (flags & CLAP_TRANSPORT_IS_LOOP_ACTIVE)       ? scuff_transport_is_loop_active       : 0;
	out |= (flags & CLAP_TRANSPORT_IS_PLAYING)           ? scuff_transport_is_playing           : 0;
	out |= (flags & CLAP_TRANSPORT_IS_RECORDING)         ? scuff_transport_is_recording         : 0;
	out |= (flags & CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL)   ? scuff_transport_is_within_pre_roll   : 0;
	return out;
}

[[nodiscard]] static
auto type_to_scuff(uint16_t type) -> uint16_t {
	switch (type) {
		case CLAP_EVENT_MIDI:                return scuff_event_type_midi;
		case CLAP_EVENT_MIDI_SYSEX:          return scuff_event_type_midi_sysex;
		case CLAP_EVENT_MIDI2:               return scuff_event_type_midi2;
		case CLAP_EVENT_NOTE_EXPRESSION:     return scuff_event_type_note_expression;
		case CLAP_EVENT_PARAM_GESTURE_BEGIN: return scuff_event_type_param_gesture_begin;
		case CLAP_EVENT_PARAM_GESTURE_END:   return scuff_event_type_param_gesture_end;
		case CLAP_EVENT_PARAM_MOD:           return scuff_event_type_param_mod;
		case CLAP_EVENT_PARAM_VALUE:         return scuff_event_type_param_value;
		case CLAP_EVENT_TRANSPORT:           return scuff_event_type_transport;
		default: throw std::runtime_error("scuff::events::clap::type_to_scuff: Invalid event type");
	}
}

[[nodiscard]] static
auto to_scuff(const clap_event_header_t& hdr) -> scuff_event_header {
	scuff_event_header out;
	out.CLAP_space_id = hdr.space_id;
	out.flags         = flags_to_scuff(hdr.flags);
	out.size          = hdr.size;
	out.time          = hdr.time;
	out.type          = type_to_scuff(hdr.type);
	return out;
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff_(const clap_event_midi_sysex_t& e, const Conv& fns) -> scuff::events::event {
	scuff_event_midi_sysex out;
	out.buffer     = e.buffer;
	out.header     = to_scuff(e.header);
	out.port_index = e.port_index;
	out.size       = e.size;
	return out;
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff_(const clap_event_midi_t& e, const Conv& fns) -> scuff::events::event {
	scuff_event_midi out;
	out.header     = to_scuff(e.header);
	out.port_index = e.port_index;
	std::copy(std::begin(e.data), std::end(e.data), std::begin(out.data));
	return out;
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff_(const clap_event_midi2_t& e, const Conv& fns) -> scuff::events::event {
	scuff_event_midi2 out;
	out.header     = to_scuff(e.header);
	out.port_index = e.port_index;
	std::copy(std::begin(e.data), std::end(e.data), std::begin(out.data));
	return out;
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff_(const clap_event_note_expression_t& e, const Conv& fns) -> scuff::events::event {
	scuff_event_note_expression out;
	out.expression_id = e.expression_id;
	out.header        = to_scuff(e.header);
	out.key           = e.key;
	out.note_id       = e.note_id;
	out.port_index    = e.port_index;
	out.value         = e.value;
	return out;
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff_(const clap_event_param_gesture_t& e, const Conv& fns) -> scuff::events::event {
	scuff_event_param_gesture out;
	out.header = to_scuff(e.header);
	out.param  = fns.find_param(e.cookie, e.param_id);
	return out;
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff_(const clap_event_param_mod_t& e, const Conv& fns) -> scuff::events::event {
	scuff_event_param_mod out;
	out.header     = to_scuff(e.header);
	out.amount     = e.amount;
	out.channel    = e.channel;
	out.key        = e.key;
	out.note_id    = e.note_id;
	out.port_index = e.port_index;
	out.param      = fns.find_param(e.cookie, e.param_id);
	return out;
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff_(const clap_event_param_value_t& e, const Conv& fns) -> scuff::events::event {
	scuff_event_param_value out;
	out.header     = to_scuff(e.header);
	out.value      = e.value;
	out.channel    = e.channel;
	out.key        = e.key;
	out.note_id    = e.note_id;
	out.port_index = e.port_index;
	out.param      = fns.find_param(e.cookie, e.param_id);
	return out;
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff_(const clap_event_transport_t& e, const Conv& fns) -> scuff::events::event {
	scuff_event_transport out;
	out.header             = to_scuff(e.header);
	out.bar_number         = e.bar_number;
	out.bar_start          = e.bar_start;
	out.flags              = transport_flags_to_scuff(e.flags);
	out.loop_end_beats     = e.loop_end_beats;
	out.loop_end_seconds   = e.loop_end_seconds;
	out.loop_start_beats   = e.loop_start_beats;
	out.loop_start_seconds = e.loop_start_seconds;
	out.song_pos_beats     = e.song_pos_beats;
	out.song_pos_seconds   = e.song_pos_seconds;
	out.tempo              = e.tempo;
	out.tempo_inc          = e.tempo_inc;
	out.tsig_denom         = e.tsig_denom;
	out.tsig_num           = e.tsig_num;
	return out;
}

template <scuff_to_clap_conversion Conv> [[nodiscard]] static
auto from_scuff(const scuff::events::event& e, const Conv& fns) -> event {
	return fast_visit([&fns](const auto& e) -> event { return from_scuff_(e, fns); }, e);
}

template <clap_to_scuff_conversion Conv> [[nodiscard]] static
auto to_scuff(const event& e, const Conv& fns) -> scuff::events::event {
	return fast_visit([&fns](const auto& e) -> scuff::events::event { return to_scuff_(e, fns); }, e);
}

using event_buffer = bc::static_vector<scuff::events::clap::event, SCUFF_EVENT_PORT_SIZE>;

} // scuff::events::clap

static
auto deserialize(std::span<const std::byte>* bytes, scuff::events::event* e) -> void {
	deserialize(bytes, e, "scuff::event");
}
