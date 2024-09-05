#pragma once

#include <cs_lr_guarded.h>
#include <vector>

namespace lg = libguarded;

// Wrapper around lr_guarded for basic publishing of audio data.
// Shared pointers to old versions of the data are kept in a list
// to ensure that they are not deleted by the audio thread.
// Every time the writer modifies the data, a garbage collection
// is run to delete old versions.
template <typename T>
struct audio_data {
	template <typename UpdateFn>
	auto modify(UpdateFn&& update_fn) -> void {
		auto lock = std::unique_lock(writer_mutex_);
		auto copy = std::make_shared<const T>(writer_data_ = update_fn(std::move(writer_data_)));
		ptr_.modify([copy](std::shared_ptr<const T>& ptr) { ptr = copy; });
		versions_.push_back(std::move(copy));
		do_garbage_collect();
	}
	auto set(T data) -> void {
		auto lock = std::unique_lock(writer_mutex_);
		auto modify_fn = [data = std::move(data)](T&&) mutable { return std::move(data); };
		modify(modify_fn);
	}
	auto read() const -> std::shared_ptr<const T> {
		return *ptr_.lock_shared().get();
	}
	auto garbage_collect() -> void {
		auto lock = std::unique_lock(writer_mutex_);
		do_garbage_collect();
	}
private:
	auto do_garbage_collect() -> void {
		auto is_garbage = [](const std::shared_ptr<const T>& ptr) { return ptr.use_count() == 1; };
		versions_.erase(std::remove_if(versions_.begin(), versions_.end(), is_garbage), versions_.end());
	}
	T writer_data_;
	std::mutex writer_mutex_;
	lg::lr_guarded<std::shared_ptr<const T>> ptr_;
	std::vector<std::shared_ptr<const T>> versions_;
};