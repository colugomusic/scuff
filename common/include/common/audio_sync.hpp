#pragma once

#include <cs_lr_guarded.h>
#include <cs_plain_guarded.h>
#include <vector>

namespace lg = libguarded;

// Wrapper around lr_guarded for basic publishing of audio data.
// Shared pointers to old versions of the data are kept in a list
// to ensure that they are not deleted by the audio thread.
// garbage_collect() should be called periodically to delete old
// versions.
template <typename T>
struct audio_data {
	template <typename UpdateFn>
	auto modify(UpdateFn&& update_fn) -> void {
		auto lock = std::unique_lock(writer_mutex_);
		auto copy = std::make_shared<const T>(writer_data_ = update_fn(std::move(writer_data_)));
		ptr_.modify([copy](std::shared_ptr<const T>& ptr) { ptr = copy; });
		versions_.push_back(std::move(copy));
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
		auto is_garbage = [](const std::shared_ptr<const T>& ptr) { return ptr.use_count() == 1; };
		versions_.erase(std::remove_if(versions_.begin(), versions_.end(), is_garbage), versions_.end());
	}
private:
	T writer_data_;
	std::mutex writer_mutex_;
	lg::lr_guarded<std::shared_ptr<const T>> ptr_;
	std::vector<std::shared_ptr<const T>> versions_;
};

template <typename T>
struct audio_sync {
	struct locked_data {
		locked_data(T value, std::unique_lock<std::mutex>&& lock) : value_(std::move(value)), lock_(std::move(lock)) {}
		auto operator*() -> T&             { return value_; }
		auto operator*() const -> const T& { return value_; }
		auto operator->() -> T*            { return &value_; }
		auto operator->() const -> const T*{ return &value_; }
	private:
		T value_;
		std::unique_lock<std::mutex> lock_;
		friend struct audio_sync<T>;
	};
	auto gc() -> void                                   { published_model_.garbage_collect(); }
	auto publish(T model) -> void                       { published_model_.set(std::move(model)); }
	auto rt_read() const -> std::shared_ptr<const T>    { return published_model_.read(); }
	auto lock() const -> locked_data                    { return locked_data{working_model_, std::unique_lock(mutex_)}; }
	auto read() const -> T                              { return *lock(); }
	auto commit(locked_data&& data) -> void             { working_model_ = std::move(data.value_); }
	auto commit_and_publish(locked_data&& data) -> void { working_model_ = std::move(data.value_); publish(working_model_); }
	auto overwrite(T model) -> void                     { *lock() = std::move(model); }
	template <typename Fn> auto update(Fn fn) -> void   { auto m = lock(); *m = fn(std::move(*m)); }
private:
	mutable std::mutex mutex_;
	T working_model_;
	audio_data<T> published_model_;
};