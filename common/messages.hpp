#pragma once

#include "c_constants.h"
#include "c_plugin_type.h"
#include "c_render_mode.h"
#include "types.hpp"
#include "events.hpp"
#include <clap/id.h>
#include <cs_plain_guarded.h>
#include <deque>
#include <variant>
#include <vector>

namespace lg = libguarded;

namespace scuff::msg::in {

struct close_all_editors      {};
struct commit_changes         {};
struct device_create          { scuff::id::device dev; scuff_plugin_type type; std::string plugfile_path; std::string plugin_id; size_t callback; };
struct device_connect         { scuff::id::device out_dev; size_t out_port; scuff::id::device in_dev; size_t in_port; };
struct device_disconnect      { scuff::id::device out_dev; size_t out_port; scuff::id::device in_dev; size_t in_port; };
struct device_erase           { scuff::id::device dev; };
struct device_gui_hide        { scuff::id::device dev; };
struct device_gui_show        { scuff::id::device dev; };
struct device_load            { scuff::id::device dev; std::vector<std::byte> state; };
struct device_save            { scuff::id::device dev; size_t callback; };
struct device_set_render_mode { scuff::id::device dev; scuff_render_mode mode; };
struct event                  { scuff::id::device dev; scuff::events::event event; };
struct find_param             { scuff::id::device dev; std::string param_id; size_t callback; };
struct get_param_value        { scuff::id::device dev; scuff::idx::param param; size_t callback; };
struct get_param_value_text   { scuff::id::device dev; scuff::idx::param param; double value; size_t callback; };
struct set_sample_rate        { double sr; };

using msg = std::variant<
	close_all_editors,
	commit_changes,
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
	find_param,
	get_param_value,
	get_param_value_text,
	set_sample_rate
>;

[[nodiscard]] inline
auto serialize(const msg& msg) -> std::vector<std::byte> {
	// TODO:
	return {};
}

} // scuff::msg::in

namespace scuff::msg::out {

struct device_create_error     { scuff::id::device dev; size_t callback; };
struct device_create_success   { scuff::id::device dev; size_t callback; };
struct device_params_changed   { scuff::id::device dev; };
struct return_param            { scuff::idx::param param; size_t callback; };
struct return_param_value      { double value; size_t callback; };
struct return_param_value_text { std::string text; size_t callback; };
struct return_state            { scuff::id::device dev; std::vector<std::byte> bytes; size_t callback; };

using msg = std::variant<
	device_create_error,
	device_create_success,
	device_params_changed,
	return_param,
	return_param_value,
	return_param_value_text,
	return_state
>;

inline
auto deserialize(const std::vector<std::byte>& bytes, msg* out) -> void {
	// TODO:
}

} // scuff::msg::out

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
			buffer_          = serialize(local_queue->front());
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

