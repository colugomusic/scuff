#pragma once

#include "c_constants.h"
#include "messages.hpp"
#include <array>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/segment_manager.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/static_string.hpp>
#include <deque>
#include <mutex>
#include <numeric>
#include <string>

namespace bc  = boost::container;
namespace bip = boost::interprocess;

namespace scuff::shm {

static constexpr auto SEGMENT_OVERHEAD = 2048;
static constexpr auto OBJECT_AUDIO_IN  = "+audio+in";
static constexpr auto OBJECT_AUDIO_OUT = "+audio+out";
static constexpr auto OBJECT_DATA      = "+data";

using string = boost::static_string<SCUFF_SHM_STRING_MAX>;
using blob   = bc::static_vector<std::byte, SCUFF_SHM_BLOB_MAX>;

struct segment {
	struct remove_when_done_t {};
	static constexpr auto remove_when_done = remove_when_done_t{};
	segment() = default;
	segment(const segment&) = delete;
	segment(segment&&) noexcept = default;
	segment& operator=(const segment&) = delete;
	segment& operator=(segment&&) noexcept = default;
	segment(std::string_view id) : segment{id, false} {}
	segment(remove_when_done_t, std::string_view id) : segment{id, true} {}
	segment(std::string_view id, size_t segment_size) : segment{id, segment_size, false} {}
	segment(remove_when_done_t, std::string_view id, size_t segment_size) : segment{id, segment_size, true} {}
	~segment() {
		if (remove_when_done_) {
			bip::shared_memory_object::remove(id_.c_str());
		}
	}
	[[nodiscard]] auto seg() -> bip::managed_shared_memory& { return seg_; }
	[[nodiscard]] auto id() const -> std::string_view { return id_; }
private:
	segment(std::string_view id, size_t segment_size, bool remove_when_done)
		: id_{id}
		, seg_{bip::create_only, id.data(), segment_size}
		, remove_when_done_{remove_when_done}
	{}
	segment(std::string_view id, bool remove_when_done)
		: id_{id}
		, seg_{bip::open_only, id.data()}
		, remove_when_done_{remove_when_done}
	{
	}
	bip::managed_shared_memory seg_;
	std::string id_;
	bool remove_when_done_ = false;
};

template <typename MsgT>
struct sbox_msg_buffer {
	auto take(std::vector<MsgT>* out) -> void {
		const auto lock = std::unique_lock{mutex_};
		std::copy(list_.begin(), list_.end(), std::back_inserter(*out));
		list_.clear();
	}
	[[nodiscard]]
	auto push_as_many_as_possible(const std::deque<MsgT>& msgs) -> size_t {
		const auto lock = std::unique_lock{mutex_};
		const auto count = std::min(msgs.size(), list_.capacity() - list_.size());
		std::copy(msgs.begin(), msgs.begin() + count, std::back_inserter(list_));
		return count;
	}
private:
	bc::static_vector<MsgT, 100> list_;
	bip::interprocess_mutex mutex_;
};

using audio_buffer = std::array<float, SCUFF_VECTOR_SIZE * SCUFF_CHANNEL_COUNT>;
using event_buffer = bc::static_vector<scuff::events::event, SCUFF_EVENT_PORT_SIZE>;

template <typename T>
struct ab {
	static constexpr auto A = 0;
	static constexpr auto B = 0;
	std::array<T, 2> value;
};

template <typename T, size_t N>
struct slot_buffer {
	slot_buffer()
		: sem_{static_cast<uint32_t>(N)}
	{
		free_indices_.resize(N);
		std::iota(free_indices_.rbegin(), free_indices_.rend(), 0);
	}
	auto put(T item) -> size_t {
		sem_.wait(); // Wait for space to be available
		const auto lock = std::unique_lock{mutex_};
		assert (free_indices_.size() > 0);
		const auto index = pop_free_index();
		items_[index] = item;
		return index;
	}
	auto take(size_t index) -> T {
		const auto lock = std::unique_lock{mutex_};
		assert (index < N);
		const auto item = items_[index];
		push_free_index(index);
		sem_.post(); // Signal that space is available
		return item;
	}
private:
	auto pop_free_index() -> size_t {
		assert (!free_indices_.empty());
		const auto index = free_indices_.back();
		free_indices_.pop_back();
		return index;
	}
	auto push_free_index(size_t index) -> void {
		free_indices_.push_back(index);
	}
	bip::interprocess_mutex mutex_;
	bip::interprocess_semaphore sem_;
	bc::static_vector<T, N> items_;
	bc::static_vector<size_t, N> free_indices_;
};

struct device_flags {
	enum e {
		has_gui          = 1 << 0,
		has_params       = 1 << 1,
		is_active        = 1 << 2,
		supports_offline = 1 << 3, // TODO: initialize these flags when device is created
	};
	int value = 0;
};

struct device_data {
	size_t param_count = 0;
	device_flags flags;
	ab<event_buffer> events_in;
	ab<event_buffer> events_out;
	bip::interprocess_mutex mutex;
};

struct sandbox_data {
	sbox_msg_buffer<msg::in::msg> msgs_in;
	sbox_msg_buffer<msg::out::msg> msgs_out;
	slot_buffer<shm::string, SCUFF_SHM_STRING_BUF_SZ> strings;
	slot_buffer<shm::blob, SCUFF_SHM_BLOB_BUF_SZ> strings;
};

struct group_data {
	// This is incremented to signal all
	// sandboxes in the group to process.
	std::atomic<uint64_t> epoch = 0;
	// Each sandbox process decrements this
	// counter when it is finished processing.
	ab<std::atomic<uint64_t>> sandboxes_processing;
};

template <typename T> static
auto find_shm_obj(bip::managed_shared_memory* seg, std::string_view id, T** out_ptr) -> size_t {
	const auto [found_ptr, count] = seg->find<T>(id.data());
	*out_ptr = found_ptr;
	return count;
}

template <typename T> [[nodiscard]] static
auto find_shm_obj_value(bip::managed_shared_memory* seg, std::string_view id, T* out_value) -> size_t {
	const auto [found_ptr, count] = seg->find<T>(id.data());
	*out_value = *found_ptr;
	return count;
}

template <typename T> static
auto require_shm_obj(bip::managed_shared_memory* seg, std::string_view id, size_t required_count, T** out_ptr) -> void {
	const auto count = find_shm_obj(seg, id, out_ptr);
	if (count < required_count) {
		throw std::runtime_error{"Could not find shared memory object: " + std::string{id}};
	}
}

struct group : segment {
	static constexpr auto SEGMENT_SIZE = sizeof(group_data) + SEGMENT_OVERHEAD;
	group_data* data = nullptr;
	group() = default;
	group(bip::create_only_t, segment::remove_when_done_t, std::string_view id) : segment{segment::remove_when_done, id, SEGMENT_SIZE} { create(); }
	group(bip::open_only_t, std::string_view id) : segment{id} { open(); }
private:
	auto create() -> void {
		data = seg().construct<group_data>(OBJECT_DATA)();
	}
	auto open() -> void {
		require_shm_obj<group_data>(&seg(), OBJECT_DATA, 1, &data);
	}
};

struct sandbox : segment {
	static constexpr auto SEGMENT_SIZE = sizeof(sandbox_data) + SEGMENT_OVERHEAD;
	sandbox_data* data = nullptr;
	sandbox() = default;
	sandbox(bip::create_only_t, segment::remove_when_done_t, std::string_view id) : segment{segment::remove_when_done, id, SEGMENT_SIZE} { create(); }
	sandbox(bip::open_only_t, std::string_view id) : segment{id} { open(); }
private:
	auto create() -> void {
		data = seg().construct<sandbox_data>(OBJECT_DATA)();
	}
	auto open() -> void {
		require_shm_obj<sandbox_data>(&seg(), OBJECT_DATA, 1, &data);
	}
};

struct device : segment {
	static constexpr auto SEGMENT_SIZE = sizeof(device_data) + SEGMENT_OVERHEAD;
	device_data* data = nullptr;
	device() = default;
	device(bip::create_only_t, std::string_view id) : segment{id, SEGMENT_SIZE} { create(); }
	device(bip::open_only_t, segment::remove_when_done_t, std::string_view id) : segment{segment::remove_when_done, id} { open(); }
private:
	auto create() -> void {
		data = seg().construct<device_data>(OBJECT_DATA)();
	}
	auto open() -> void {
		require_shm_obj<device_data>(&seg(), OBJECT_DATA, 1, &data);
	}
};

struct device_audio_ports : segment {
	size_t input_count  = 0;
	size_t output_count = 0;
	ab<audio_buffer>* input_buffers  = nullptr;
	ab<audio_buffer>* output_buffers = nullptr;
	device_audio_ports() = default;
	device_audio_ports(bip::create_only_t, std::string_view id, size_t input_port_count, size_t output_port_count)
		: segment{id, sizeof(ab<audio_buffer>) * (input_port_count + output_port_count) + SEGMENT_OVERHEAD}
	{
		create(input_port_count, output_port_count);
	}
	device_audio_ports(bip::open_only_t, segment::remove_when_done_t, std::string_view id)
		: segment{segment::remove_when_done, id}
	{
		open();
	}
private:
	auto create(size_t input_port_count, size_t output_port_count) -> void {
		assert (input_port_count > 0 || output_port_count > 0);
		input_count    = input_port_count;
		output_count   = output_port_count;
		if (input_port_count > 0)  { input_buffers  = seg().construct<ab<audio_buffer>>(OBJECT_AUDIO_IN)[input_port_count](); }
		if (output_port_count > 0) { output_buffers = seg().construct<ab<audio_buffer>>(OBJECT_AUDIO_OUT)[output_port_count](); }
	}
	auto open() -> void {
		input_count  = find_shm_obj<ab<audio_buffer>>(&seg(), OBJECT_AUDIO_IN, &input_buffers);
		output_count = find_shm_obj<ab<audio_buffer>>(&seg(), OBJECT_AUDIO_OUT, &output_buffers);
	}
};

} // scuff::shm