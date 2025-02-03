#include "common-serialize-messages.hpp"
#include <cs_plain_guarded.h>
#include <iostream>

namespace lg = libguarded;

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
	auto receive(ReceiveFn receive) -> const std::vector<MsgT>& {
		msg_buffer_.clear();
		for (;;) {
			if (bytes_remaining_ > 0) {
				const auto bytes_to_get = bytes_remaining_;
				const auto offset       = byte_buffer_.size() - bytes_remaining_;
				const auto bytes_got    = receive(byte_buffer_.data() + offset, bytes_to_get);
				assert (bytes_got <= bytes_remaining_);
				bytes_remaining_ -= bytes_got;
				if (bytes_got < bytes_to_get) {
					return msg_buffer_;
				}
				if (msg_size_ == 0) {
					// Just got the message size
					assert (byte_buffer_.size() == sizeof(size_t));
					msg_size_ = *reinterpret_cast<size_t*>(byte_buffer_.data());
					assert (msg_size_ < 1'000'000); // sanity check
					bytes_remaining_ = msg_size_;
					byte_buffer_.resize(bytes_remaining_);
					continue;
				}
				// Finished receiving all the bytes for a message
				MsgT msg;
				deserialize(byte_buffer_, &msg);
				msg_buffer_.push_back(msg);
				msg_size_        = 0;
				bytes_remaining_ = 0;
				continue;
			}
			// Setup to receive the size of the next message
			byte_buffer_.resize(sizeof(size_t));
			bytes_remaining_ = sizeof(size_t);
		}
	}
private:
	std::vector<MsgT> msg_buffer_;
	std::vector<std::byte> byte_buffer_;
	size_t bytes_remaining_ = 0;
	size_t msg_size_ = 0;
};

} // scuff::msg
