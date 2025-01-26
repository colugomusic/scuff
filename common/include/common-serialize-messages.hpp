#pragma once

#include "common-messages.hpp"
#include "common-serialize-events.hpp"
#include "common-serialize-param-info.hpp"

template <> inline
auto deserialize<scuff::msg::in::device_create>(std::span<const std::byte>* bytes, scuff::msg::in::device_create* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->type);
	deserialize(bytes, &msg->plugfile_path);
	deserialize(bytes, &msg->plugin_id);
}

template <> inline
auto deserialize<scuff::msg::in::device_load>(std::span<const std::byte>* bytes, scuff::msg::in::device_load* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->state);
}

template <> inline
auto deserialize<scuff::msg::in::set_track_color>(std::span<const std::byte>* bytes, scuff::msg::in::set_track_color* msg) -> void {
	bool engaged = false;
	scuff::rgba32 color;
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &engaged);
	if (engaged) {
		deserialize(bytes, &color);
		msg->color = color;
	}
}

template <> inline
auto deserialize<scuff::msg::in::set_track_name>(std::span<const std::byte>* bytes, scuff::msg::in::set_track_name* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->name);
}

template <> inline
auto deserialize<scuff::msg::in::event>(std::span<const std::byte>* bytes, scuff::msg::in::event* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->event);
}

template <> inline
auto deserialize<scuff::msg::out::device_create_fail>(std::span<const std::byte>* bytes, scuff::msg::out::device_create_fail* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->error);
	deserialize(bytes, &msg->callback);
}

template <> inline
auto deserialize<scuff::msg::out::device_create_success>(std::span<const std::byte>* bytes, scuff::msg::out::device_create_success* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->ports_shmid);
	deserialize(bytes, &msg->callback);
}

template <> inline
auto deserialize<scuff::msg::out::device_port_info>(std::span<const std::byte>* bytes, scuff::msg::out::device_port_info* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->info.audio_input_port_count);
	deserialize(bytes, &msg->info.audio_output_port_count);
}

template <> inline
auto deserialize<scuff::msg::out::device_param_info>(std::span<const std::byte>* bytes, scuff::msg::out::device_param_info* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->info);
}

template <> inline
auto deserialize<scuff::msg::out::report_error>(std::span<const std::byte>* bytes, scuff::msg::out::report_error* msg) -> void {
	deserialize(bytes, &msg->text);
}

template <> inline
auto deserialize<scuff::msg::out::report_info>(std::span<const std::byte>* bytes, scuff::msg::out::report_info* msg) -> void {
	deserialize(bytes, &msg->text);
}

template <> inline
auto deserialize<scuff::msg::out::report_warning>(std::span<const std::byte>* bytes, scuff::msg::out::report_warning* msg) -> void {
	deserialize(bytes, &msg->text);
}

template <> inline
auto deserialize<scuff::msg::out::return_param_value_text>(std::span<const std::byte>* bytes, scuff::msg::out::return_param_value_text* msg) -> void {
	deserialize(bytes, &msg->text);
	deserialize(bytes, &msg->callback);
}

template <> inline
auto deserialize<scuff::msg::out::return_state>(std::span<const std::byte>* bytes, scuff::msg::out::return_state* msg) -> void {
	deserialize(bytes, &msg->bytes);
	deserialize(bytes, &msg->callback);
}

static
auto deserialize(const std::vector<std::byte>& bytes, scuff::msg::in::msg* out) -> void {
	deserialize(bytes, out, "input message");
}

static
auto deserialize(const std::vector<std::byte>& bytes, scuff::msg::out::msg* out) -> void {
	deserialize(bytes, out, "output message");
}

template <> inline
auto serialize<scuff::msg::in::device_create>(const scuff::msg::in::device_create& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.type, bytes);
	serialize(std::string_view{msg.plugfile_path}, bytes);
	serialize(std::string_view{msg.plugin_id}, bytes);
}

template <> inline
auto serialize<scuff::msg::in::device_load>(const scuff::msg::in::device_load& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.state, bytes);
}

template <> inline
auto serialize<scuff::msg::in::set_track_color>(const scuff::msg::in::set_track_color& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.color.has_value(), bytes);
	if (msg.color.has_value()) {
		serialize(msg.color.value(), bytes);
	}
}

template <> inline
auto serialize<scuff::msg::in::set_track_name>(const scuff::msg::in::set_track_name& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.name, bytes);
}

template <> inline
auto serialize<scuff::msg::in::event>(const scuff::msg::in::event& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.event, bytes);
}

template <> inline
auto serialize<scuff::msg::out::device_create_fail>(const scuff::msg::out::device_create_fail& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(std::string_view{msg.error}, bytes);
	serialize(msg.callback, bytes);
}

template <> inline
auto serialize<scuff::msg::out::device_create_success>(const scuff::msg::out::device_create_success& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(std::string_view{msg.ports_shmid}, bytes);
	serialize(msg.callback, bytes);
}

template <> inline
auto serialize<scuff::msg::out::device_port_info>(const scuff::msg::out::device_port_info& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.info.audio_input_port_count, bytes);
	serialize(msg.info.audio_output_port_count, bytes);
}

template <> inline
auto serialize<scuff::msg::out::device_param_info>(const scuff::msg::out::device_param_info& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.info, bytes);
}

template <> inline
auto serialize<scuff::msg::out::report_error>(const scuff::msg::out::report_error& msg, std::vector<std::byte>* bytes) -> void {
	serialize(std::string_view{msg.text}, bytes);
}

template <> inline
auto serialize<scuff::msg::out::report_info>(const scuff::msg::out::report_info& msg, std::vector<std::byte>* bytes) -> void {
	serialize(std::string_view{msg.text}, bytes);
}

template <> inline
auto serialize<scuff::msg::out::report_warning>(const scuff::msg::out::report_warning& msg, std::vector<std::byte>* bytes) -> void {
	serialize(std::string_view{msg.text}, bytes);
}

template <> inline
auto serialize<scuff::msg::out::return_param_value_text>(const scuff::msg::out::return_param_value_text& msg, std::vector<std::byte>* bytes) -> void {
	serialize(std::string_view{msg.text}, bytes);
	serialize(msg.callback, bytes);
}

template <> inline
auto serialize<scuff::msg::out::return_state>(const scuff::msg::out::return_state& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.bytes, bytes);
	serialize(msg.callback, bytes);
}
