#pragma once

#include "c_constants.h"
#include "c_plugin_type.h"
#include "c_render_mode.h"
#include "c_types.h"
#include "types.hpp"
#include "events.hpp"
#include <clap/id.h>
#include <cs_plain_guarded.h>
#include <deque>
#include <iterator>
#include <variant>
#include <vector>

namespace lg = libguarded;

namespace scuff::msg::in {

// These messages are sent from the client to a sandbox process.

struct clean_shutdown         {};
struct close_all_editors      {};
struct device_create          { scuff_device dev_id; scuff_plugin_type type; std::string plugfile_path; std::string plugin_id; size_t callback; };
struct device_connect         { int64_t out_dev_id; size_t out_port; int64_t in_dev_id; size_t in_port; };
struct device_disconnect      { int64_t out_dev_id; size_t out_port; int64_t in_dev_id; size_t in_port; };
struct device_erase           { scuff_device dev_id; }; // This is sent to every sandbox in the device's sandbox group, so that they can clear any
                                                        // connections to the device.
struct device_gui_hide        { scuff_device dev_id; };
struct device_gui_show        { scuff_device dev_id; };
struct device_load            { scuff_device dev_id; std::vector<std::byte> state; };
struct device_save            { scuff_device dev_id; size_t callback; };
struct device_set_render_mode { scuff_device dev_id; scuff_render_mode mode; };
struct event                  { scuff_device dev_id; scuff::event event; };
struct get_param_value        { scuff_device dev_id; scuff_param param_idx; size_t callback; };
struct get_param_value_text   { scuff_device dev_id; scuff_param param_idx; double value; size_t callback; };
struct set_sample_rate        { double sr; };

using msg = std::variant<
	clean_shutdown,
	close_all_editors,
	device_create,
	device_connect,
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
	set_sample_rate
>;

} // scuff::msg::in

namespace scuff::msg::out {

// These messages are sent back from a sandbox process to the client.

struct device_param_info_changed { scuff_device dev_id; std::string new_shmid; };
struct report_error              { std::string text; };
struct report_info               { std::string text; };
struct report_warning            { std::string text; };
struct return_created_device     { scuff_device dev_id; std::string ports_shmid; std::string param_info_shmid; size_t callback; };
struct return_param_value        { double value; size_t callback; };
struct return_param_value_text   { std::string text; size_t callback; };
struct return_state              { scuff_device dev_id; std::vector<std::byte> bytes; size_t callback; };

using msg = std::variant<
	device_param_info_changed,
	report_error,
	report_info,
	report_warning,
	return_created_device,
	return_param_value,
	return_param_value_text,
	return_state
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
	deserialize(bytes, &msg->param_info_shmid);
	deserialize(bytes, &msg->callback);
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
	deserialize(bytes, &msg->dev_id);
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
	serialize(std::string_view{msg.param_info_shmid}, bytes);
	serialize(msg.callback, bytes);
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
	serialize(msg.dev_id, bytes);
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
			auto data     = serialize(local_queue->front());
			auto msg_size = data.size();
			buffer_.clear();
			serialize(msg_size, &buffer_);
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
				msg_size_ = 0;
				bytes_remaining_ = sizeof(size_t);
				buffer_.resize(bytes_remaining_);
			}
			else {
				msg_size_ = *reinterpret_cast<size_t*>(buffer_.data());
				bytes_remaining_ = msg_size_;
				buffer_.resize(bytes_remaining_);
			}
		}
	}
private:
	std::vector<std::byte> buffer_;
	size_t bytes_remaining_ = 0;
	size_t msg_size_ = 0;
};

} // scuff::msg
