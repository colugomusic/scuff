#pragma once

#include "plugin_type.hpp"
#include "render_mode.hpp"
#include "serialize_events.hpp"
#include <clap/id.h>
#include <cs_plain_guarded.h>
#include <deque>
#include <iterator>
#include <variant>
#include <vector>

namespace lg = libguarded;

namespace scuff::msg::in {

// These messages are sent from the client to a sandbox process.

struct activate               { double sr; };
struct clean_shutdown         {};
struct close_all_editors      {};
struct crash                  {}; // Tell the sandbox process to crash. Important for testing.
struct deactivate             {};
struct device_connect         { int64_t out_dev_id; size_t out_port; int64_t in_dev_id; size_t in_port; };
struct device_create          { id::device::type dev_id; plugin_type type; std::string plugfile_path; std::string plugin_id; size_t callback; };
struct device_disconnect      { int64_t out_dev_id; size_t out_port; int64_t in_dev_id; size_t in_port; };
struct device_erase           { id::device::type dev_id; };
struct device_gui_hide        { id::device::type dev_id; };
struct device_gui_show        { id::device::type dev_id; };
struct device_load            { id::device::type dev_id; std::vector<std::byte> state; size_t callback; };
struct device_save            { id::device::type dev_id; size_t callback; };
struct device_set_render_mode { id::device::type dev_id; render_mode mode; };
struct event                  { id::device::type dev_id; scuff::event event; };
struct get_param_value        { id::device::type dev_id; size_t param_idx; size_t callback; };
struct get_param_value_text   { id::device::type dev_id; size_t param_idx; double value; size_t callback; };
struct heartbeat              {}; // Sandbox shuts itself down if this isn't received within a certain time.

using msg = std::variant<
	activate,
	clean_shutdown,
	close_all_editors,
	crash,
	deactivate,
	device_connect,
	device_create,
	device_disconnect,
	device_erase,
	device_gui_hide,
	device_gui_show,
	device_load,
	device_save,
	device_set_render_mode,
	event,
	get_param_value,
	get_param_value_text,
	heartbeat
>;

} // scuff::msg::in

namespace scuff::msg::out {

// These messages are sent back from a sandbox process to the client.

struct confirm_activated         {};
struct device_param_info_changed { id::device::type dev_id; std::string new_shmid; };
struct report_error              { std::string text; };
struct report_fatal_error        { std::string text; };
struct report_info               { std::string text; };
struct report_warning            { std::string text; };
struct return_created_device     { id::device::type dev_id; std::string ports_shmid; size_t callback; };
struct return_param_value        { double value; size_t callback; };
struct return_param_value_text   { std::string text; size_t callback; };
struct return_state              { std::vector<std::byte> bytes; size_t callback; };
struct return_void               { size_t callback; };

using msg = std::variant<
	confirm_activated,
	device_param_info_changed,
	report_error,
	report_fatal_error,
	report_info,
	report_warning,
	return_created_device,
	return_param_value,
	return_param_value_text,
	return_state,
	return_void
>;

} // scuff::msg::out

template <> inline
auto deserialize<scuff::msg::in::device_create>(std::span<const std::byte>* bytes, scuff::msg::in::device_create* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->type);
	deserialize(bytes, &msg->plugfile_path);
	deserialize(bytes, &msg->plugin_id);
	deserialize(bytes, &msg->callback);
}

template <> inline
auto deserialize<scuff::msg::in::device_load>(std::span<const std::byte>* bytes, scuff::msg::in::device_load* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->state);
}

template <> inline
auto deserialize<scuff::msg::in::event>(std::span<const std::byte>* bytes, scuff::msg::in::event* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->event);
}

template <> inline
auto deserialize<scuff::msg::out::device_param_info_changed>(std::span<const std::byte>* bytes, scuff::msg::out::device_param_info_changed* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->new_shmid);
}

template <> inline
auto deserialize<scuff::msg::out::return_created_device>(std::span<const std::byte>* bytes, scuff::msg::out::return_created_device* msg) -> void {
	deserialize(bytes, &msg->dev_id);
	deserialize(bytes, &msg->ports_shmid);
	deserialize(bytes, &msg->callback);
}

template <> inline
auto deserialize<scuff::msg::out::report_error>(std::span<const std::byte>* bytes, scuff::msg::out::report_error* msg) -> void {
	deserialize(bytes, &msg->text);
}

template <> inline
auto deserialize<scuff::msg::out::report_fatal_error>(std::span<const std::byte>* bytes, scuff::msg::out::report_fatal_error* msg) -> void {
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
	serialize(msg.callback, bytes);
}

template <> inline
auto serialize<scuff::msg::in::device_load>(const scuff::msg::in::device_load& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.state, bytes);
}

template <> inline
auto serialize<scuff::msg::in::event>(const scuff::msg::in::event& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(msg.event, bytes);
}

template <> inline
auto serialize<scuff::msg::out::device_param_info_changed>(const scuff::msg::out::device_param_info_changed& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(std::string_view{msg.new_shmid}, bytes);
}

template <> inline
auto serialize<scuff::msg::out::return_created_device>(const scuff::msg::out::return_created_device& msg, std::vector<std::byte>* bytes) -> void {
	serialize(msg.dev_id, bytes);
	serialize(std::string_view{msg.ports_shmid}, bytes);
	serialize(msg.callback, bytes);
}

template <> inline
auto serialize<scuff::msg::out::report_error>(const scuff::msg::out::report_error& msg, std::vector<std::byte>* bytes) -> void {
	serialize(std::string_view{msg.text}, bytes);
}

template <> inline
auto serialize<scuff::msg::out::report_fatal_error>(const scuff::msg::out::report_fatal_error& msg, std::vector<std::byte>* bytes) -> void {
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

namespace scuff::msg {

template <typename MsgT>
struct sender {
	auto enqueue(const MsgT& msg) -> void {
		local_queue_.lock()->push_back(msg);
	}
	template <typename SendFn>
	auto send(SendFn send) -> void {
		for (;;) {
			if (bytes_remaining_ > 0) {
				const auto bytes_to_send = bytes_remaining_;
				const auto offset        = buffer_.size() - bytes_remaining_;
				const auto bytes_sent    = send(buffer_.data() + offset, bytes_to_send);
				bytes_remaining_ -= bytes_sent;
				if (bytes_sent < bytes_to_send) {
					return;
				}
			}
			const auto local_queue = local_queue_.lock();
			if (local_queue->empty()) {
				return;
			}
			auto data = serialize(local_queue->front());
			buffer_.clear();
			serialize(data, &buffer_);
			bytes_remaining_ = buffer_.size();
			local_queue->pop_front();
		}
	}
private:
	std::vector<std::byte> buffer_;
	size_t bytes_remaining_ = 0;
	lg::plain_guarded<std::deque<MsgT>> local_queue_;
};

template <typename MsgT>
struct receiver {
	template <typename ReceiveFn>
	auto receive(ReceiveFn receive) -> std::vector<MsgT> {
		std::vector<MsgT> msgs;
		for (;;) {
			if (bytes_remaining_ > 0) {
				const auto bytes_to_get = bytes_remaining_;
				const auto offset       = buffer_.size() - bytes_remaining_;
				const auto bytes_got    = receive(buffer_.data() + offset, bytes_to_get);
				bytes_remaining_ -= bytes_got;
				if (bytes_got < bytes_to_get) {
					return msgs;
				}
			}
			if (msg_size_ > 0) {
				MsgT msg;
				deserialize(buffer_, &msg);
				msgs.push_back(msg);
				msg_size_        = 0;
				bytes_remaining_ = 0;
				continue;
			}
			buffer_.resize(sizeof(size_t));
			if (receive(buffer_.data(), sizeof(size_t)) < sizeof(size_t)) {
				return msgs;
			}
			msg_size_        = *reinterpret_cast<size_t*>(buffer_.data());
			bytes_remaining_ = msg_size_;
			buffer_.resize(bytes_remaining_);
		}
	}
private:
	std::vector<std::byte> buffer_;
	size_t bytes_remaining_ = 0;
	size_t msg_size_ = 0;
};

} // scuff::msg

