#pragma once

#include "client.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>

namespace scuff {

struct device_state {
	device_state() = default;
	device_state(const device_state& rhs) = default;
	device_state& operator=(const device_state& rhs) = default;
	device_state(device_state&& rhs) noexcept = default;
	device_state& operator=(device_state&& rhs) noexcept = default;
	device_state(scuff::bytes&& bytes) : body_{std::make_shared<body>()} { body_->bytes = std::move(bytes); }
	// Asynchronous save
	device_state(id::device id)
		: body_{std::make_shared<body>()}
	{
		auto fn = [body = body_](const scuff::bytes& bytes) {
			if (body.use_count() == 1) {
				// If this is the only remaining reference
				// then we can ignore the result.
				return;
			}
			auto lock = std::unique_lock{body->mutex};
			body->bytes = bytes;
			body->awaiting = false;
			body->cv.notify_all();
		};
		body_->awaiting = true;
		scuff::save_async(id, fn);
	}
	// Will have to block if we are still waiting for the data to be returned.
	auto get_bytes() const -> const scuff::bytes& {
		auto lock  = std::unique_lock{body_->mutex};
		auto ready = [this] { return !body_->awaiting; };
		if (!ready()) {
			body_->cv.wait(lock, ready);
		}
		return body_->bytes;
	}
	explicit operator bool() const { return bool(body_); }
private:
	struct body {
		std::mutex mutex;
		std::condition_variable cv;
		scuff::bytes bytes;
		bool awaiting = false;
	};
	std::shared_ptr<body> body_;
};

} // scuff
