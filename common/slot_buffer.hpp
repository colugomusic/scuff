#pragma once

#include <cassert>
#include <mutex>
#include <vector>

template <typename T>
struct slot_buffer {
	slot_buffer() {
		std::iota(free_indices.rbegin(), free_indices.rend(), 0);
	}
	auto put(T value) -> size_t {
		auto lock = std::unique_lock(mutex);
		if (free_indices.empty()) {
			add_capacity();
		}
		const auto index = pop_free_index();
		buffer[index] = value;
		return index;
	}
	auto take(size_t index) -> T {
		auto lock = std::unique_lock(mutex);
		const auto value = buffer[index];
		push_free_index(index);
		return value;
	}
private:
	auto add_capacity() -> void {
		static constexpr auto STRANGE_CAPACITY = 1024;
		assert (buffer.size() < STRANGE_CAPACITY && "slot_buffer capacity is really high");
		const auto old_capacity = buffer.size();
		const auto extra        = old_capacity;
		const auto new_capacity = old_capacity + extra;
		free_indices.resize(extra);
		buffer.resize(new_capacity);
		std::iota(free_indices.rbegin(), free_indices.rend(), old_capacity);
	}
	auto pop_free_index() -> size_t {
		assert (!free_indices.empty());
		const auto index = free_indices.back();
		free_indices.pop_back();
		return index;
	}
	auto push_free_index(size_t index) -> void {
		free_indices.push_back(index);
	}
	std::mutex mutex;
	std::vector<T> buffer{32};
	std::vector<size_t> free_indices{32};
};
