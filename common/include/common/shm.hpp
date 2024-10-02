#pragma once

#include "common/event_buffer.hpp"
#include "common/param_info.hpp"
#include "common/signaling.hpp"
#include "messages.hpp"
#include <array>
#include <boost/container/static_vector.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/segment_manager.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/static_string.hpp>
#include <deque>
#include <numeric>
#include <string>

namespace bc  = boost::container;

namespace scuff::shm {

static constexpr auto SEGMENT_OVERHEAD = 2048;
static constexpr auto OBJECT_AUDIO_IN  = "+audio+in";
static constexpr auto OBJECT_AUDIO_OUT = "+audio+out";
static constexpr auto OBJECT_DATA      = "+data";

struct segment {
	struct remove_when_done_t {};
	static constexpr auto remove_when_done = remove_when_done_t{};
	segment() = default;
	segment(const segment&) = delete;
	segment& operator=(const segment&) = delete;
	segment(std::string_view id) : segment{id, false} {}
	segment(remove_when_done_t, std::string_view id) : segment{id, true} {}
	segment(std::string_view id, size_t segment_size) : segment{id, segment_size, false} {}
	segment(remove_when_done_t, std::string_view id, size_t segment_size) : segment{id, segment_size, true} {}
	segment(segment&& rhs) noexcept
		: seg_{std::move(rhs.seg_)}
		, id_{std::move(rhs.id_)}
		, remove_when_done_{rhs.remove_when_done_}
	{
		rhs.remove_when_done_ = false;
	}
	segment& operator=(segment&& rhs) noexcept {
		if (this != &rhs) {
			seg_              = std::move(rhs.seg_);
			id_               = std::move(rhs.id_);
			remove_when_done_ = rhs.remove_when_done_;
			rhs.remove_when_done_ = false;
		}
		return *this;
	}
	~segment() {
		if (remove_when_done_) {
			bip::shared_memory_object::remove(id_.c_str());
		}
	}
	[[nodiscard]] auto seg() -> bip::managed_shared_memory& { return seg_; }
	[[nodiscard]] auto id() const -> std::string_view { return id_; }
	[[nodiscard]] auto is_valid() const -> bool { return !id_.empty(); }
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

struct msg_buffer {
	[[nodiscard]]
	auto read(std::byte* bytes, size_t count) -> size_t {
		const auto lock = std::unique_lock{mutex_};
		count = std::min(count, bytes_.size());
		std::copy(bytes_.begin(), bytes_.begin() + count, bytes);
		bytes_.erase(bytes_.begin(), bytes_.begin() + count);
		return count;
	}
	[[nodiscard]]
	auto write(const std::byte* bytes, size_t count) -> size_t {
		const auto lock = std::unique_lock{mutex_};
		count = std::min(count, bytes_.capacity() - bytes_.size());
		bytes_.insert(bytes_.end(), bytes, bytes + count);
		return count;
	}
private:
	bc::static_vector<std::byte, MSG_BUFFER_SIZE> bytes_;
	bip::interprocess_mutex mutex_;
};

using audio_buffer = std::array<float, VECTOR_SIZE * CHANNEL_COUNT>;

struct device_flags {
	enum e {
		has_gui    = 1 << 0,
		has_params = 1 << 1,
	};
	int value = 0;
};

struct device_atomic_flags {
	enum e {
		is_active = 1 << 0,
	};
	std::atomic_int value = 0;
};

struct device_data {
	device_flags flags;
	device_atomic_flags atomic_flags;
	scuff::event_buffer events_in;
	scuff::event_buffer events_out;
	bc::static_vector<scuff::param_info, MAX_PARAMS> param_info;
	bc::static_vector<audio_buffer, MAX_AUDIO_PORTS> audio_in;
	bc::static_vector<audio_buffer, MAX_AUDIO_PORTS> audio_out;
	bip::interprocess_mutex param_info_mutex; // Lock this when rescanning parameters
};

struct sandbox_data {
	msg_buffer msgs_in;
	msg_buffer msgs_out;
};

struct group_data {
	signaling::group_data signaling;
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
	[[nodiscard]] static
	auto make_id(std::string_view instance_id, id::group group_id) -> std::string {
		return std::format("{}+group+{}", instance_id, group_id.value);
	}
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
	[[nodiscard]] auto send_bytes(const std::byte* bytes, size_t count) const -> size_t { return data->msgs_out.write(bytes, count); }
	[[nodiscard]] auto receive_bytes(std::byte* bytes, size_t count) const -> size_t { return data->msgs_in.read(bytes, count); }
	[[nodiscard]] static
	auto make_id(std::string_view instance_id, id::sandbox sbox_id) -> std::string {
		return std::format("{}+sbox+{}", instance_id, sbox_id.value);
	}
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
	[[nodiscard]] static
	auto make_id(std::string_view instance_id, id::device dev_id) -> std::string {
		return std::format("{}+dev+{}", instance_id, dev_id.value);
	}
private:
	auto create() -> void {
		data = seg().construct<device_data>(OBJECT_DATA)();
	}
	auto open() -> void {
		require_shm_obj<device_data>(&seg(), OBJECT_DATA, 1, &data);
	}
};

/* For less memory usage, could do something like this.
 * Not doing this at the moment because it complicates things a lot.
struct device_audio_ports : segment {
	size_t input_count  = 0;
	size_t output_count = 0;
	audio_buffer* input_buffers  = nullptr;
	audio_buffer* output_buffers = nullptr;
	device_audio_ports() = default;
	device_audio_ports(bip::create_only_t, std::string_view id, size_t input_port_count, size_t output_port_count)
		: segment{id, sizeof(audio_buffer) * (input_port_count + output_port_count) + SEGMENT_OVERHEAD}
	{
		create(input_port_count, output_port_count);
	}
	device_audio_ports(bip::open_only_t, std::string_view id)
		: segment{id}
	{
		open();
	}
	[[nodiscard]] static
	auto make_id(std::string_view instance_id, id::sandbox sbox_id, id::device dev_id, uint64_t uid) -> std::string {
		return std::format("{}+sbox+{}+dev+{}+ports+{}", instance_id, sbox_id.value, dev_id.value, uid);
	}
private:
	auto create(size_t input_port_count, size_t output_port_count) -> void {
		assert (input_port_count > 0 || output_port_count > 0);
		input_count    = input_port_count;
		output_count   = output_port_count;
		if (input_port_count > 0)  { input_buffers  = seg().construct<audio_buffer>(OBJECT_AUDIO_IN)[input_port_count](); }
		if (output_port_count > 0) { output_buffers = seg().construct<audio_buffer>(OBJECT_AUDIO_OUT)[output_port_count](); }
	}
	auto open() -> void {
		input_count  = find_shm_obj<audio_buffer>(&seg(), OBJECT_AUDIO_IN, &input_buffers);
		output_count = find_shm_obj<audio_buffer>(&seg(), OBJECT_AUDIO_OUT, &output_buffers);
	}
};
*/

} // scuff::shm