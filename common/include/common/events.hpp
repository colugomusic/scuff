#pragma once

#include "c_events.h"
#include "serialization.hpp"
#include "visit.hpp"
#include <clap/events.h>
#include <string_view>
#include <variant>

namespace scuff {

struct vst_dummy_event {};

using event = std::variant<
	clap_event_midi_sysex_t,
	clap_event_midi_t,
	clap_event_midi2_t,
	clap_event_note_expression_t,
	clap_event_param_gesture_t,
	clap_event_param_mod_t,
	clap_event_param_value_t,
	clap_event_transport_t,
	vst_dummy_event
>;

namespace clap {

[[nodiscard]] static
auto convert(const scuff_event_clap& e) -> scuff::event {
	if (e.event->type == CLAP_EVENT_MIDI_SYSEX) { return *reinterpret_cast<const clap_event_midi_sysex_t*>(e.event); }
	if (e.event->type == CLAP_EVENT_MIDI) { return *reinterpret_cast<const clap_event_midi_t*>(e.event); }
	if (e.event->type == CLAP_EVENT_MIDI2) { return *reinterpret_cast<const clap_event_midi2_t*>(e.event); }
	if (e.event->type == CLAP_EVENT_NOTE_EXPRESSION) { return *reinterpret_cast<const clap_event_note_expression_t*>(e.event); }
	if (e.event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN) { return *reinterpret_cast<const clap_event_param_gesture_t*>(e.event); }
	if (e.event->type == CLAP_EVENT_PARAM_GESTURE_END) { return *reinterpret_cast<const clap_event_param_gesture_t*>(e.event); }
	if (e.event->type == CLAP_EVENT_PARAM_MOD) { return *reinterpret_cast<const clap_event_param_mod_t*>(e.event); }
	if (e.event->type == CLAP_EVENT_PARAM_VALUE) { return *reinterpret_cast<const clap_event_param_value_t*>(e.event); }
	if (e.event->type == CLAP_EVENT_TRANSPORT) { return *reinterpret_cast<const clap_event_transport_t*>(e.event); }
	throw std::runtime_error("scuff::clap::convert(clap_event_header): Invalid event header");
}

[[nodiscard]] static
auto convert(const event& e) -> const clap_event_header& {
	if (std::holds_alternative<clap_event_midi_sysex_t>(e)) { return std::get<clap_event_midi_sysex_t>(e).header; }
	if (std::holds_alternative<clap_event_midi_t>(e)) { return std::get<clap_event_midi_t>(e).header; }
	if (std::holds_alternative<clap_event_midi2_t>(e)) { return std::get<clap_event_midi2_t>(e).header; }
	if (std::holds_alternative<clap_event_note_expression_t>(e)) { return std::get<clap_event_note_expression_t>(e).header; }
	if (std::holds_alternative<clap_event_param_gesture_t>(e)) { return std::get<clap_event_param_gesture_t>(e).header; }
	if (std::holds_alternative<clap_event_param_mod_t>(e)) { return std::get<clap_event_param_mod_t>(e).header; }
	if (std::holds_alternative<clap_event_param_value_t>(e)) { return std::get<clap_event_param_value_t>(e).header; }
	if (std::holds_alternative<clap_event_transport_t>(e)) { return std::get<clap_event_transport_t>(e).header; }
	throw std::runtime_error("scuff::clap::convert(event): Invalid event");
}

} // clap

namespace vst {

[[nodiscard]] static
auto convert(const scuff_event_vst& e) -> scuff::event {
	// Not implemented yet
	return vst_dummy_event{};
}

} // vst

[[nodiscard]] static
auto is_clap_event(const scuff::event& e) -> bool {
	return e.index() < 8;
}

[[nodiscard]] static
auto is_vst_event(const scuff::event& e) -> bool {
	return e.index() >= 8;
}

[[nodiscard]] static
auto convert(const scuff_event_header& header) -> scuff::event {
	switch (header.type) {
		case scuff_event_type_clap: { return clap::convert(*reinterpret_cast<const scuff_event_clap*>(&header)); }
		case scuff_event_type_vst:  { return vst::convert(*reinterpret_cast<const scuff_event_vst*>(&header)); }
	}
	assert (false && "Invalid event header");
	return {};
}

} // scuff

static
auto deserialize(std::span<const std::byte>* bytes, scuff::event* e) -> void {
	deserialize(bytes, e, "scuff::event");
}
