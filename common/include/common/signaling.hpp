#pragma once

#include <atomic>
#include <mutex>
#include <stop_token>

namespace scuff::signaling {

struct group_data;

enum class wait_for_signaled_result {
	signaled,
	stop_requested,
	timeout
};

// Signal all sandboxes in the group to begin processing.
              static auto signal_sandbox_processing(signaling::group_data* data, int sandbox_count, uint64_t epoch) -> void;

// Wait for all sandboxes in the group to finish processing.
[[nodiscard]] static auto wait_for_all_sandboxes_done(signaling::group_data* data) -> bool;

// The sandbox process calls this to wait for a signal from the client that it should begin processing.
[[nodiscard]] static auto wait_for_signaled(signaling::group_data* data, std::stop_token stop_token, uint64_t* local_epoch) -> wait_for_signaled_result;

// The sandbox process calls this to notify that it has finished processing.
// If it is the last sandbox to finish processing, the client is notified.
              static auto notify_sandbox_finished_processing(signaling::group_data* data) -> void;

} // scuff::signaling

#if defined(SCUFF_SIGNALING_MODE_BOOST_IPC) //////////////////////////////////////////////////
// This is the easiest to understand implementation but the performance is bad.
// boost::interprocess_condition uses CreateSemaphore and WaitForSingleObject under the hood.
// On macOS and Linux it uses sem_wait and sem_post.
// It may also be doing other bad stuff which slows things down.

#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

namespace bip = boost::interprocess;

namespace scuff::signaling {

struct group_data {
	// This is incremented before signaling the
	// sandboxes in the group to process.
	std::atomic<uint64_t> epoch = 0;
	// Each sandbox process decrements this
	// counter when it is finished processing.
	std::atomic<int> sandboxes_processing;
	bip::interprocess_mutex mut;
	bip::interprocess_condition cv;
};

static
auto signal_sandbox_processing(signaling::group_data* data, int sandbox_count, uint64_t epoch) -> void {
	// Set the sandbox counter
	data->sandboxes_processing.store(sandbox_count);
	auto lock = std::unique_lock{data->mut};
	// Set the epoch.
	data->epoch.store(epoch, std::memory_order_release);
	// Signal sandboxes to start processing.
	data->cv.notify_all();
}

[[nodiscard]] static
auto wait_for_all_sandboxes_done(signaling::group_data* data) -> bool {
	 // the sandboxes not completing their work within 1 second is a
	 // catastrophic failure.
	static constexpr auto MAX_WAIT_TIME = std::chrono::seconds{1};
	auto done = [data]() -> bool {
		return data->sandboxes_processing.load(std::memory_order_acquire) <= 0;
	};
	if (done()) {
		return true;
	}
	auto lock = std::unique_lock{data->mut};
	return data->cv.wait_for(lock, MAX_WAIT_TIME, done);
}

[[nodiscard]] static
auto wait_for_signaled(signaling::group_data* data, std::stop_token stop_token, uint64_t* local_epoch) -> wait_for_signaled_result {
	static constexpr auto TIMEOUT = std::chrono::seconds(1);
	uint64_t epoch = 0;
	auto wait_condition = [data, &epoch, local_epoch, &stop_token]{
		epoch = data->epoch;
		return epoch > *local_epoch || stop_token.stop_requested();
	};
	auto lock = std::unique_lock{data->mut};
	if (!data->cv.wait_for(lock, TIMEOUT, wait_condition)) {
		return wait_for_signaled_result::timeout;
	}
	if (epoch > *local_epoch) {
		*local_epoch = epoch;
		return wait_for_signaled_result::signaled;
	}
	return wait_for_signaled_result::stop_requested;;
}

static
auto notify_sandbox_finished_processing(signaling::group_data* data) -> void {
	const auto prev_value = data->sandboxes_processing.fetch_sub(1, std::memory_order_release);
	if (prev_value == 1) {
		// Notify the client that all sandboxes have finished their work.
		data->cv.notify_one();
	}
}

} // scuff::signaling

#elif defined(SCUFF_SIGNALING_MODE_SPEEN) //////////////////////////////////////////////////

namespace scuff::signaling {

struct group_data {
	// This is incremented before signaling the
	// sandboxes in the group to process.
	std::atomic<uint64_t> epoch = 0;
	// Each sandbox process decrements this
	// counter when it is finished processing.
	std::atomic<int> sandboxes_processing;
};

static
auto signal_sandbox_processing(signaling::group_data* data, int sandbox_count, uint64_t epoch) -> void {
	if (sandbox_count > 0) {
		// Set the sandbox counter
		data->sandboxes_processing.store(sandbox_count);
		// Set the epoch.
		data->epoch.store(epoch, std::memory_order_release);
	}
}

[[nodiscard]] static
auto wait_for_all_sandboxes_done(signaling::group_data* data) -> bool {
	auto done = [data]() -> bool {
		return data->sandboxes_processing.load(std::memory_order_acquire) <= 0;
	};
	if (done()) {
		return true;
	}
	for (int i = 0; i < 10000000; i++) {
		std::this_thread::yield();
		if (done()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] static
auto wait_for_signaled(signaling::group_data* data, std::stop_token stop_token, uint64_t* local_epoch) -> wait_for_signaled_result {
	uint64_t epoch = 0;
	auto wait_condition = [data, &epoch, local_epoch, &stop_token]{
		epoch = data->epoch;
		return epoch > *local_epoch || stop_token.stop_requested();
	};
	for (int i = 0; i < 10000000; i++) {
		std::this_thread::yield();
		if (wait_condition()) {
			break;
		}
	}
	if (!wait_condition()) {
		return wait_for_signaled_result::timeout;
	}
	if (epoch > *local_epoch) {
		*local_epoch = epoch;
		return wait_for_signaled_result::signaled;
	}
	return wait_for_signaled_result::stop_requested;;
}

static
auto notify_sandbox_finished_processing(signaling::group_data* data) -> void {
	data->sandboxes_processing.fetch_sub(1, std::memory_order_release);
}

} // scuff::signaling

#endif /////////////////////////////////////////////////////////////////////////////
