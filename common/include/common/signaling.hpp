#pragma once

#include <atomic>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <mutex>

namespace bip = boost::interprocess;

namespace scuff {
namespace signaling {

#if defined(SCUFF_SIGNALING_MODE_BOOST_IPC)

struct group_data {
	// This is incremented before signaling the
	// sandboxes in the group to process.
	std::atomic<uint64_t> epoch = 0;
	// Each sandbox process decrements this
	// counter when it is finished processing.
	std::atomic<uint64_t> sandboxes_processing;
	bip::interprocess_mutex mut;
	bip::interprocess_condition cv;
};

static
auto signal_sandbox_processing(signaling::group_data* data, size_t sandbox_count, uint64_t epoch) -> void {
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
		return data->sandboxes_processing.load(std::memory_order_acquire) < 1;
	};
	if (done()) {
		return true;
	}
	auto lock = std::unique_lock{data->mut};
	return data->cv.wait_for(lock, MAX_WAIT_TIME, done);
}

[[nodiscard]] static
auto wait_for_signaled(signaling::group_data* data, std::stop_token stop_token, uint64_t* local_epoch) -> bool {
	uint64_t epoch = 0;
	auto wait_condition = [data, &epoch, local_epoch, &stop_token]{
		epoch = data->epoch;
		return epoch > *local_epoch || stop_token.stop_requested();
	};
	auto lock = std::unique_lock{data->mut};
	data->cv.wait(lock, wait_condition);
	if (epoch > *local_epoch) {
		*local_epoch = epoch;
		return true;
	}
	return false;
}

static
auto notify_sandbox_finished_processing(signaling::group_data* data) -> void {
	const auto prev_value = data->sandboxes_processing.fetch_sub(1, std::memory_order_release);
	if (prev_value == 1) {
		// Notify the client that all sandboxes have finished their work.
		data->cv.notify_one();
	}
}

#endif

} // signaling
} // scuff
