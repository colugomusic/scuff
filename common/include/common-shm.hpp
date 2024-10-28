#pragma once

#include "common-event-buffer.hpp"
#include "common-param-info.hpp"
#include "common-signaling.hpp"
#include "common-messages.hpp"
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
namespace bip = boost::interprocess;

namespace scuff::shm {

static constexpr auto SEGMENT_OVERHEAD = 2048;
static constexpr auto OBJECT_AUDIO_IN  = "+audio+in";
static constexpr auto OBJECT_AUDIO_OUT = "+audio+out";
static constexpr auto OBJECT_DATA      = "+data";

struct segment_raii {
	bip::managed_shared_memory seg;
	std::string id;
	bool remove_when_done = false;
	segment_raii() = default;
	segment_raii(const segment_raii&) = delete;
	segment_raii& operator=(const segment_raii&) = delete;
	segment_raii(segment_raii&& rhs) noexcept
		: seg{std::move(rhs.seg)}
		, id{std::move(rhs.id)}
		, remove_when_done{rhs.remove_when_done}
	{
		rhs.remove_when_done = false;
	}
	segment_raii& operator=(segment_raii&& rhs) noexcept {
		if (this != &rhs) {
			seg              = std::move(rhs.seg);
			id               = std::move(rhs.id);
			remove_when_done = rhs.remove_when_done;
			rhs.remove_when_done = false;
		}
		return *this;
	}
	~segment_raii() {
		if (remove_when_done) {
			bip::shared_memory_object::remove(id.c_str());
		}
	}
};

struct open_or_create_segment_result {
	bip::managed_shared_memory seg;
	bool was_created;
};

[[nodiscard]] static
auto is_valid(const segment_raii& seg) -> bool {
	return !seg.id.empty();
}

[[nodiscard]] static
auto open_or_create_segment(std::string_view id, size_t segment_size) -> open_or_create_segment_result {
	open_or_create_segment_result result;
	try {
		result.seg         = bip::managed_shared_memory{bip::open_only, id.data()};
		result.was_created = false;
	}
	catch (const bip::interprocess_exception&) {
		result.seg         = bip::managed_shared_memory{bip::create_only, id.data(), segment_size};
		result.was_created = true;
	}
	return result;
}

struct msg_buffer {
	[[nodiscard]]
	auto read(std::byte* bytes, size_t count) -> size_t {
		const auto time = std::chrono::steady_clock::now() + std::chrono::seconds{1};
		const auto lock = bip::scoped_lock{mutex_, time};
		if (!lock) {
			return 0;
		}
		count = std::min(count, bytes_.size());
		if (count <= 0) {
			return 0;
		}
		std::copy(bytes_.begin(), bytes_.begin() + count, bytes);
		bytes_.erase(bytes_.begin(), bytes_.begin() + count);
		return count;
	}
	[[nodiscard]]
	auto write(const std::byte* bytes, size_t count) -> size_t {
		const auto time = std::chrono::steady_clock::now() + std::chrono::seconds{1};
		const auto lock = bip::scoped_lock{mutex_, time};
		if (!lock) {
			return 0;
		}
		if (bytes_.size() + count > bytes_.capacity()) {
			__debugbreak();
		}
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
		has_gui       = 1 << 0,
		has_params    = 1 << 1,
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
	signaling::group_shm_data signaling;
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

struct group {
	segment_raii seg;
	group_data* data = nullptr;
	signaling::group_local_data signaling;
};

struct sandbox {
	segment_raii seg;
	sandbox_data* data = nullptr;
};

struct device {
	segment_raii seg;
	device_data* data = nullptr;
};

static constexpr auto GROUP_SEGMENT_SIZE   = sizeof(group_data) + SEGMENT_OVERHEAD;
static constexpr auto SANDBOX_SEGMENT_SIZE = sizeof(sandbox_data) + SEGMENT_OVERHEAD;
static constexpr auto DEVICE_SEGMENT_SIZE  = sizeof(device_data) + SEGMENT_OVERHEAD;

[[nodiscard]] static
auto create_group(std::string_view id, bool remove_when_done) -> group {
	group shm;
	shm.seg.seg              = bip::managed_shared_memory{bip::create_only, id.data(), GROUP_SEGMENT_SIZE};
	shm.seg.id               = id;
	shm.seg.remove_when_done = remove_when_done;
	shm.data                 = shm.seg.seg.construct<group_data>(OBJECT_DATA)();
	shm.signaling            = signaling::group_local_data{signaling::client{&shm.data->signaling}};
	return shm;
}

[[nodiscard]] static
auto open_group(std::string_view id) -> group {
	group shm;
	shm.seg.seg             = bip::managed_shared_memory{bip::open_only, id.data()};
	shm.seg.id              = id;
	require_shm_obj<group_data>(&shm.seg.seg, OBJECT_DATA, 1, &shm.data);
	shm.signaling = signaling::group_local_data{signaling::sandbox{&shm.data->signaling}};
	return shm;
}

[[nodiscard]] static
auto make_group_id(std::string_view instance_id, id::group group_id) -> std::string {
	return std::format("{}+group+{}", instance_id, group_id.value);
}

[[nodiscard]] static
auto create_sandbox(std::string_view id, bool remove_when_done) -> sandbox {
	sandbox shm;
	shm.seg.seg              = bip::managed_shared_memory{bip::create_only, id.data(), SANDBOX_SEGMENT_SIZE};
	shm.seg.id               = id;
	shm.seg.remove_when_done = remove_when_done;
	shm.data = shm.seg.seg.construct<sandbox_data>(OBJECT_DATA)();
	return shm;
}

[[nodiscard]] static
auto open_sandbox(std::string_view id) -> sandbox {
	sandbox shm;
	shm.seg.seg             = bip::managed_shared_memory{bip::open_only, id.data()};
	shm.seg.id              = id;
	require_shm_obj<sandbox_data>(&shm.seg.seg, OBJECT_DATA, 1, &shm.data);
	return shm;
}

[[nodiscard]] static
auto make_sandbox_id(std::string_view instance_id, id::sandbox sbox_id) -> std::string {
	return std::format("{}+sbox+{}", instance_id, sbox_id.value);
}

[[nodiscard]] static
auto send_bytes_to_client(const sandbox& shm, const std::byte* bytes, size_t count) -> size_t {
	return shm.data->msgs_out.write(bytes, count);
}

[[nodiscard]] static
auto send_bytes_to_sandbox(const sandbox& shm, const std::byte* bytes, size_t count) -> size_t {
	return shm.data->msgs_in.write(bytes, count);
}

[[nodiscard]] static
auto receive_bytes_from_client(const sandbox& shm, std::byte* bytes, size_t count) -> size_t {
	return shm.data->msgs_in.read(bytes, count);
}

[[nodiscard]] static
auto receive_bytes_from_sandbox(const sandbox& shm, std::byte* bytes, size_t count) -> size_t {
	return shm.data->msgs_out.read(bytes, count);
}

static
auto on_device_created(device* shm) -> void {
	shm->data = shm->seg.seg.construct<device_data>(OBJECT_DATA)();
}

static
auto on_device_opened(device* shm) -> void {
	require_shm_obj<device_data>(&shm->seg.seg, OBJECT_DATA, 1, &shm->data);
}

[[nodiscard]] static
auto open_device(std::string_view id, bool remove_when_done) -> device {
	device shm;
	shm.seg.seg              = bip::managed_shared_memory{bip::open_only, id.data()};
	shm.seg.id               = id;
	shm.seg.remove_when_done = remove_when_done;
	on_device_opened(&shm);
	return shm;
}

[[nodiscard]] static
auto open_or_create_device(std::string_view id, bool remove_when_done) -> device {
	device shm;
	auto result              = open_or_create_segment(id, DEVICE_SEGMENT_SIZE);
	shm.seg.seg              = std::move(result.seg);
	shm.seg.id               = id;
	shm.seg.remove_when_done = remove_when_done;
	if (result.was_created) {
		on_device_created(&shm);
	}
	else {
		on_device_opened(&shm);
	}
	return shm;
}

[[nodiscard]] static
auto make_device_id(std::string_view sbox_shmid, id::device dev_id) -> std::string {
	return std::format("{}+dev+{}", sbox_shmid, dev_id.value);
}

} // scuff::shm