#pragma once

#include "c_constants.h"
#include "c_string_options.h"
#include "messages.hpp"
#include <array>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/segment_manager.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/container/static_vector.hpp>
#include <numeric>
#include <string>

namespace bc  = boost::container;
namespace bip = boost::interprocess;

namespace scuff::shm {

static constexpr auto OBJECT_AUDIO_IN          = "+audio+in";
static constexpr auto OBJECT_AUDIO_OUT         = "+audio+out";
static constexpr auto OBJECT_EVENTS_IN         = "+events+in";
static constexpr auto OBJECT_EVENTS_OUT        = "+events+out";
static constexpr auto OBJECT_READY_EPOCH_IN    = "+epoch+in";
static constexpr auto OBJECT_READY_EPOCH_OUT   = "+epoch+out";
static constexpr auto OBJECT_CONTROL_BLOCK     = "+cb";
static constexpr auto OBJECT_ITEMS             = "+items";
static constexpr auto OBJECT_INDICES           = "+indices";
static constexpr auto OBJECT_MAX_STRING_LENGTH = "+msl";
static constexpr auto OBJECT_SBOX_MSGS_IN      = "+msgs+in";
static constexpr auto OBJECT_SBOX_MSGS_OUT     = "+msgs+out";

struct segment {
	bip::managed_shared_memory seg;
	std::string id;
};

struct sbox_messages_in {
	bc::static_vector<scuff::msg::in::msg, 100> list;
	bip::interprocess_mutex mutex;
};

struct sbox_messages_out {
	bc::static_vector<scuff::msg::out::msg, 100> list;
	bip::interprocess_mutex mutex;
};

[[nodiscard]] static
auto index_pop(size_t* list, size_t* count) -> size_t {
	return list[--(*count)];
}

static
auto index_push(size_t* list, size_t* count, size_t index) -> void {
	list[(*count)++] = index;
}

struct buffer_cb {
	buffer_cb(size_t capacity) : capacity{capacity}, free_index_count{capacity}, sem{static_cast<uint32_t>(capacity)} {}
	// Capacity of the buffer
	size_t capacity;
	// Total number of free indices remaining
	size_t free_index_count;
	// Lock when accessing the buffer
	bip::interprocess_mutex mutex;
	// If the semaphore is 0 then the buffer is full
	bip::interprocess_semaphore sem;
};

template <typename T>
struct item_buffer : segment {
	// Control block
	buffer_cb* cb;
	// List of items
	T* items;
	// List of free indices
	size_t* free_indices;
};

[[nodiscard]] static
auto calc_item_buffer_seg_size(size_t capacity, size_t item_size) -> size_t {
	size_t size = 0;
	size += sizeof(scuff::shm::buffer_cb);
	size += capacity * sizeof(size_t);
	size += capacity * item_size;
	return size;
}

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
auto create(item_buffer<T>* sb, std::string_view id, size_t capacity) -> void {
	sb->seg            = bip::managed_shared_memory{bip::create_only, id.data(), calc_item_buffer_seg_size(capacity, sizeof(T))};
	sb->id             = id;
	sb->items          = sb->seg.construct<T>        (OBJECT_ITEMS)[capacity];
	sb->cb             = sb->seg.construct<buffer_cb>(OBJECT_CONTROL_BLOCK)(capacity);
	sb->free_indices   = sb->seg.construct<size_t>   (OBJECT_INDICES)[capacity](0);
	std::iota(sb->free_indices, sb->free_indices + capacity, 0);
	std::reverse(sb->free_indices, sb->free_indices + capacity);
}

template <typename T>
[[nodiscard]] static
auto open(item_buffer<T>* ib, std::string_view id) -> bool {
	ib->seg = bip::managed_shared_memory{bip::open_only, id.data()};
	ib->id  = id;
	if (find_shm_obj<buffer_cb>(ib->seg, OBJECT_CONTROL_BLOCK, &ib->cb) < 1)                 { return false; }
	if (find_shm_obj<T>(ib->seg, OBJECT_ITEMS, &ib->items) < ib->cb->capacity)               { return false; }
	if (find_shm_obj<size_t>(ib->seg, OBJECT_INDICES, &ib->free_indices) < ib->cb->capacity) { return false; }
	return true;
}

template <typename T, typename U, typename WriteFn>
[[nodiscard]] static
auto put(item_buffer<T>* ib, const U& item, WriteFn write) -> size_t {
	ib->cb->sem.wait(); // Wait for space to be available
	ib->cb->mutex.lock();
	assert (ib->cb->free_index_count > 0);
	const auto index = index_pop(ib->free_indices, &ib->cb->free_index_count);
	write(ib->items, index, item);
	ib->cb->mutex.unlock();
	return index;
}

template <typename T, typename U>
[[nodiscard]] static
auto put(item_buffer<T>* ib, const U& item) -> size_t {
	auto default_write_fn = [](T* items, size_t index, const T& item) { items[index] = item; };
	return put(ib, item, default_write_fn);
}

template <typename T, typename ReadFn>
[[nodiscard]] static
auto take(item_buffer<T>* ib, size_t index, ReadFn read) -> decltype(auto) {
	ib->cb->mutex.lock();
	assert (index < ib->cb->capacity);
	assert (ib->cb->free_index_count < ib->cb->capacity);
	const auto item = read(ib->items, index);
	index_push(ib->free_indices, &ib->cb->free_index_count, index);
	ib->cb->sem.post(); // Signal that space is available
	ib->cb->mutex.unlock();
	return item;
}

template <typename T>
[[nodiscard]] static
auto take(item_buffer<T>* ib, size_t index) -> T {
	auto default_read_fn = [](const T* items, size_t index) { return items[index]; };
	return take(ib, index, default_read_fn);
}

struct string_buffer : item_buffer<char> {
	size_t max_string_length;
};

[[nodiscard]] static
auto calc_total_chars(const scuff_string_options& options) -> size_t {
	return options.max_in_flight_strings * (options.max_string_length + 1);
}

[[nodiscard]] static
auto calc_seg_size(const scuff_string_options& options) -> size_t {
	const auto capacity  = options.max_in_flight_strings;
	const auto item_size = options.max_string_length + 1;
	auto size = calc_item_buffer_seg_size(capacity, item_size);
	size += sizeof(size_t); // max_string_length
	return size;
}

static
auto create(string_buffer* sb, std::string_view id, const scuff_string_options& options) -> void {
	const auto char_count = calc_total_chars(options);
	const auto capacity   = options.max_in_flight_strings;
	sb->seg               = bip::managed_shared_memory{bip::create_only, id.data(), calc_seg_size(options)};
	sb->cb                = sb->seg.construct<buffer_cb>(OBJECT_CONTROL_BLOCK)(capacity);
	sb->items             = sb->seg.construct<char>     (OBJECT_ITEMS)[char_count]('\0');
	sb->max_string_length = *sb->seg.construct<size_t>  (OBJECT_MAX_STRING_LENGTH)(options.max_string_length);
	sb->free_indices      = sb->seg.construct<size_t>   (OBJECT_INDICES)[capacity](0);
	std::iota(sb->free_indices, sb->free_indices + capacity, 0);
	std::reverse(sb->free_indices, sb->free_indices + capacity);
}

[[nodiscard]] static
auto open(string_buffer* sb, std::string_view id) -> bool {
	sb->seg = bip::managed_shared_memory{bip::open_only, id.data()};
	if (find_shm_obj<buffer_cb>(&sb->seg, OBJECT_CONTROL_BLOCK, &sb->cb) < 1)                       { return false; }
	if (find_shm_obj_value<size_t>(&sb->seg, OBJECT_MAX_STRING_LENGTH, &sb->max_string_length) < 1) { return false; }
	if (find_shm_obj<size_t>(&sb->seg, OBJECT_INDICES, &sb->free_indices) < sb->cb->capacity)       { return false; }
	const auto expected_char_count = calc_total_chars({sb->max_string_length, sb->cb->capacity});
	if (find_shm_obj<char>(&sb->seg, OBJECT_ITEMS, &sb->items) < expected_char_count)               { return false; }
	return true;
}

[[nodiscard]] static
auto put(string_buffer* sb, const std::string& s) -> size_t {
	auto write_fn = [sb](char* items, size_t index, const std::string& s) {
		const auto char_index  = index * (sb->max_string_length + 1);
		const auto string_size = std::min(s.size(), sb->max_string_length);
		std::copy_n(s.c_str(), string_size, items + char_index);
	};
	return put(sb, s, write_fn);
}

[[nodiscard]] static
auto take(string_buffer* sb, size_t index) -> std::string {
	auto read_fn = [sb](const char* items, size_t index) {
		const auto char_index = index * (sb->max_string_length + 1);
		return std::string{items + char_index};
	};
	return take(sb, index, read_fn);
}

template <typename T>
struct ab {
	static constexpr auto A = 0;
	static constexpr auto B = 0;
	std::array<T, 2> value;
};

struct grp_control_block {
	// This is incremented to signal all
	// sandboxes in the group to process.
	std::atomic<uint64_t> epoch = 0;
	// Each sandbox process decrements this
	// counter when it is finished processing.
	ab<std::atomic<uint64_t>> sandboxes_processing;
};

struct group : segment {
	grp_control_block* cb = nullptr;
};

static
auto create(group* group, std::string_view id) -> void {
	static constexpr auto SEG_SIZE = sizeof(grp_control_block);
	group->id  = id;
	group->seg = bip::managed_shared_memory{bip::create_only, id.data(), SEG_SIZE};
	group->cb  = group->seg.construct<grp_control_block>(OBJECT_CONTROL_BLOCK)();
}

[[nodiscard]] static
auto open(group* group, std::string_view id) -> bool {
	group->id  = id;
	group->seg = bip::managed_shared_memory{bip::open_only, id.data()};
	if (find_shm_obj<grp_control_block>(&group->seg, OBJECT_CONTROL_BLOCK, &group->cb) < 1) { return false; }
	return true;
}

struct sandbox : segment {
	sbox_messages_in* msgs_in   = nullptr;
	sbox_messages_out* msgs_out = nullptr;
};

static
auto create(sandbox* sbox, std::string_view id) -> void {
	static constexpr auto SEG_SIZE =
		sizeof(shm::sbox_messages_in) +
		sizeof(shm::sbox_messages_out);
	sbox->id       = id;
	sbox->seg      = bip::managed_shared_memory{bip::create_only, id.data(), SEG_SIZE};
	sbox->msgs_in  = sbox->seg.construct<sbox_messages_in>(OBJECT_SBOX_MSGS_IN)();
	sbox->msgs_out = sbox->seg.construct<sbox_messages_out>(OBJECT_SBOX_MSGS_OUT)();
}

[[nodiscard]] static
auto open(sandbox* sbox, std::string_view id) -> bool {
	sbox->id  = id;
	sbox->seg = bip::managed_shared_memory{bip::open_only, id.data()};
	if (find_shm_obj<sbox_messages_in>(&sbox->seg, OBJECT_SBOX_MSGS_IN, &sbox->msgs_in) < 1)    { return false; }
	if (find_shm_obj<sbox_messages_out>(&sbox->seg, OBJECT_SBOX_MSGS_OUT, &sbox->msgs_out) < 1) { return false; }
	return true;
}

using audio_buffer = std::array<float, TOM_VECTOR_SIZE * TOM_CHANNEL_COUNT>;
using event_buffer = bc::static_vector<scuff::events::event, TOM_EVENT_PORT_SIZE>;

struct atomic_epoch { std::atomic<uint64_t> value = 0; };

struct device : segment {
	ab<event_buffer>* events_in  = nullptr;
	ab<event_buffer>* events_out = nullptr;
};

static
auto create(device* dev, std::string_view id) -> void {
	const auto seg_size = (sizeof(ab<event_buffer>) * 2 * 2);
	dev->id         = id;
	dev->seg        = bip::managed_shared_memory{bip::create_only, id.data(), seg_size};
	dev->events_in  = dev->seg.construct<ab<event_buffer>>(OBJECT_EVENTS_IN)();
	dev->events_out = dev->seg.construct<ab<event_buffer>>(OBJECT_EVENTS_OUT)();
}

static
auto open(device* dev, std::string_view id) -> bool {
	dev->id  = id;
	dev->seg = bip::managed_shared_memory{bip::open_only, id.data()};
	if (find_shm_obj<ab<event_buffer>>(&dev->seg, OBJECT_EVENTS_IN, &dev->events_in) < 1)   { return false; }
	if (find_shm_obj<ab<event_buffer>>(&dev->seg, OBJECT_EVENTS_OUT, &dev->events_out) < 1) { return false; }
	return true;
}

struct device_audio_ports : segment {
	size_t input_count    = 0;
	size_t output_count   = 0;
	ab<audio_buffer>* input_buffers  = nullptr;
	ab<audio_buffer>* output_buffers = nullptr;
};

static
auto create(device_audio_ports* ports, std::string_view id, size_t input_port_count, size_t output_port_count) -> void {
	const auto seg_size =
		(sizeof(ab<audio_buffer>) * input_port_count) +
		(sizeof(ab<audio_buffer>) * output_port_count);
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