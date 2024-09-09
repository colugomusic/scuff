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

struct segment {
	bip::managed_shared_memory seg;
	std::string id;
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

struct device : segment {
	device_data* data = nullptr;
};

struct device_audio_ports : segment {
	size_t input_count    = 0;
	size_t output_count   = 0;
	ab<audio_buffer>* input_buffers  = nullptr;
	ab<audio_buffer>* output_buffers = nullptr;
};

struct sandbox_data {
	sbox_msg_buffer<msg::in::msg> msgs_in;
	sbox_msg_buffer<msg::out::msg> msgs_out;
	slot_buffer<shm::string, SCUFF_MAX_SBOX_STRINGS> strings;
};

struct sandbox : segment {
	sandbox_data* data = nullptr;
};

struct group_data {
	// This is incremented to signal all
	// sandboxes in the group to process.
	std::atomic<uint64_t> epoch = 0;
	// Each sandbox process decrements this
	// counter when it is finished processing.
	ab<std::atomic<uint64_t>> sandboxes_processing;
};

struct group : segment {
	group_data* data = nullptr;
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

static
auto create(group* group, std::string_view id) -> void {
	static constexpr auto SEG_SIZE = sizeof(group_data) + SEGMENT_OVERHEAD;
	group->id   = id;
	group->seg  = bip::managed_shared_memory{bip::create_only, id.data(), SEG_SIZE};
	group->data = group->seg.construct<group_data>(OBJECT_DATA)();
}

[[nodiscard]] static
auto open(group* group, std::string_view id) -> bool {
	group->id  = id;
	group->seg = bip::managed_shared_memory{bip::open_only, id.data()};
	if (find_shm_obj<group_data>(&group->seg, OBJECT_DATA, &group->data) < 1) { return false; }
	return true;
}

static
auto create(sandbox* sbox, std::string_view id) -> void {
	static constexpr auto SEG_SIZE = sizeof(shm::sandbox_data) + SEGMENT_OVERHEAD;
	sbox->id   = id;
	sbox->seg  = bip::managed_shared_memory{bip::create_only, id.data(), SEG_SIZE};
	sbox->data = sbox->seg.construct<sandbox_data>(OBJECT_DATA)();
}

[[nodiscard]] static
auto open(sandbox* sbox, std::string_view id) -> bool {
	sbox->id  = id;
	sbox->seg = bip::managed_shared_memory{bip::open_only, id.data()};
	if (find_shm_obj<sandbox_data>(&sbox->seg, OBJECT_DATA, &sbox->data) < 1) { return false; }
	return true;
}

static
auto create(device* dev, std::string_view id) -> void {
	const auto SEG_SIZE = sizeof(device_data) + SEGMENT_OVERHEAD;
	dev->id   = id;
	dev->seg  = bip::managed_shared_memory{bip::create_only, id.data(), SEG_SIZE};
	dev->data = dev->seg.construct<device_data>(OBJECT_DATA)();
}

static
auto open(device* dev, std::string_view id) -> bool {
	dev->id  = id;
	dev->seg = bip::managed_shared_memory{bip::open_only, id.data()};
	if (find_shm_obj<device_data>(&dev->seg, OBJECT_DATA, &dev->data) < 1) { return false; }
	return true;
}

static
auto create(device_audio_ports* ports, std::string_view id, size_t input_port_count, size_t output_port_count) -> void {
	const auto seg_size =
		(sizeof(ab<audio_buffer>) * input_port_count) +
		(sizeof(ab<audio_buffer>) * output_port_count) +
		SEGMENT_OVERHEAD;
	assert (input_port_count > 0 || output_port_count > 0);
	ports->id           = id;
	ports->seg          = bip::managed_shared_memory{bip::create_only, id.data(), seg_size};
	ports->input_count  = input_port_count;
	ports->output_count = output_port_count;
	if (input_port_count > 0)  { ports->input_buffers  = ports->seg.construct<ab<audio_buffer>>(OBJECT_AUDIO_IN)[input_port_count](); }
	if (output_port_count > 0) { ports->output_buffers = ports->seg.construct<ab<audio_buffer>>(OBJECT_AUDIO_OUT)[output_port_count](); }
}

static
auto open(device_audio_ports* ports, std::string_view id) -> bool {
	ports->id           = id;
	ports->seg          = bip::managed_shared_memory{bip::open_only, id.data()};
	ports->input_count  = find_shm_obj<ab<audio_buffer>>(&ports->seg, OBJECT_AUDIO_IN, &ports->input_buffers);
	ports->output_count = find_shm_obj<ab<audio_buffer>>(&ports->seg, OBJECT_AUDIO_OUT, &ports->output_buffers);
	return true;
}

struct segment_remover {
	segment_remover() = default;
	segment_remover(const segment_remover&) = delete;
	segment_remover(segment_remover&&) noexcept = default;
	segment_remover& operator=(const segment_remover&) = delete;
	segment_remover& operator=(segment_remover&&) noexcept = default;
	segment_remover(std::string id) : id{id} {}
	~segment_remover() {
		if (!id.empty()) {
			bip::shared_memory_object::remove(id.c_str());
		}
	}
private:
	std::string id;
};

} // scuff::shm