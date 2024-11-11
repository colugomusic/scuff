#pragma once

#include "common-ipc-event.hpp"
#include <atomic>
#include <mutex>
#include <stop_token>

// TERMINOLOGY:
// 
// "Work unit"
//   A work unit begins when the client signals all the sandboxes to begin processing.
//   A work unit ends when either:
//     - All sandboxes have finished processing, or
//     - The client timed out while waiting for the sandboxes to finish processing, meaning at least one of the sandboxes is not responding.

// Every platform more or less follows this same signaling model:
//
// 1. sandboxes_work_begin:
//      Sandboxes wait on this.
//      Signaled in the following situations:
//        - Signaled by the client to start a work unit, or
//        - Signaled by a sandbox which is shutting down and wants to unblock itself. Other sandboxes will need to detect this situation and re-wait.
//      This signal must be reset before sandboxes_work_finished is signaled.
//
// 2. sandboxes_work_finish:
//      Sandboxes wait on this.
//      Signaled in the following situations:
//        - Signaled by the client to end a work unit, once all sandboxes have finished processing, or
//        - Signaled by a sandbox which is shutting down and wants to unblock itself. Other sandboxes will need to detect this situation and re-wait.
//      This signal must be reset before sandboxes_work_begin is signaled.
//
// 3. client_all_sandboxes_done:
//      The client waits on this.
//      Signaled in the following situations:
//        - A sandbox finished processing, and it was the last sandbox to finish processing, or
//        - The client is shutting down and wants to unblock itself.
//      This signal must be reset before the next work unit begins.

namespace scuff::signaling {

struct group_local_data;
struct group_shm_data;
struct client  { group_local_data* local_data; group_shm_data* shm_data; };
struct sandbox { group_local_data* local_data; group_shm_data* shm_data; };
struct client_init  { std::string_view group_shmid; signaling::client client; };
struct sandbox_init { std::string_view group_shmid; signaling::sandbox sandbox; };

enum class client_wait_result {
	done,
	not_responding,
};

enum class sandbox_wait_result {
	signaled,
	stop_requested,
};

struct group_local_data {
	ipc::local_event client_all_sandboxes_done;
	ipc::local_event sandboxes_work_begin;
	ipc::local_event sandboxes_work_finish;
};

struct group_shm_data {
	// This is incremented before signaling the sandboxes so that
	// they can verify that they are in the right place within each
	// work unit.
	std::atomic<uint64_t> counter = 0;
	// Each sandbox process decrements this
	// counter when it is finished processing.
	std::atomic<uint32_t> sandboxes_processing;
	// Events
	ipc::shared_event client_all_sandboxes_done;
	ipc::shared_event sandboxes_work_begin;
	ipc::shared_event sandboxes_work_finish;
};

static
// Initialize the group signalling system for a client.
auto init(signaling::client_init init) -> void {
	ipc::create_shared_event(&init.client.shm_data->client_all_sandboxes_done);
	ipc::create_shared_event(&init.client.shm_data->sandboxes_work_begin);
	ipc::create_shared_event(&init.client.shm_data->sandboxes_work_finish);
	init.client.local_data->client_all_sandboxes_done = ipc::local_event{ipc::local_event_create{&init.client.shm_data->client_all_sandboxes_done}};
	init.client.local_data->sandboxes_work_begin      = ipc::local_event{ipc::local_event_create{&init.client.shm_data->sandboxes_work_begin}};
	init.client.local_data->sandboxes_work_finish     = ipc::local_event{ipc::local_event_create{&init.client.shm_data->sandboxes_work_finish}};
}

static
// Initialize the group signalling system for a sandbox.
auto init(signaling::sandbox_init init) -> void {
	init.sandbox.local_data->client_all_sandboxes_done = ipc::local_event{ipc::local_event_open{&init.sandbox.shm_data->client_all_sandboxes_done}};
	init.sandbox.local_data->sandboxes_work_begin      = ipc::local_event{ipc::local_event_open{&init.sandbox.shm_data->sandboxes_work_begin}};
	init.sandbox.local_data->sandboxes_work_finish     = ipc::local_event{ipc::local_event_open{&init.sandbox.shm_data->sandboxes_work_finish}};
}

[[nodiscard]] static
// The client calls this to unblock itself in cases where it is waiting for a signal from
// the sandbox processes but wants to abort the operation (e.g. if one of the sandboxes
// crashed, in which case the signal would never come.)
auto unblock_self(signaling::client client) -> bool {
	return client.local_data->client_all_sandboxes_done.set();
}

[[nodiscard]] static
// The sandbox process calls this to unblock itself in cases where it is waiting for a
// signal from the client but wants to abort the operation (e.g. if the sandbox process
// is shutting down.)
auto unblock_self(signaling::sandbox sandbox) -> bool {
	const auto r0 = sandbox.local_data->sandboxes_work_begin.set();
	const auto r1 = sandbox.local_data->sandboxes_work_finish.set();
	return r0 && r1;
}

[[nodiscard]] static
// Signal all sandboxes in the group to begin processing.
auto sandboxes_work_begin(signaling::client client, int sandbox_count) -> bool {
	if (!client.local_data->sandboxes_work_finish.reset()) {
		return false;
	}
	client.shm_data->sandboxes_processing.store(sandbox_count);
	client.shm_data->counter.fetch_add(1);
	return client.local_data->sandboxes_work_begin.set();
}

[[nodiscard]] static
// Wait for all sandboxes in the group to finish processing.
auto wait_for_all_sandboxes_done(signaling::client client) -> client_wait_result {
	if (!client.local_data->client_all_sandboxes_done.wait()) {
		throw std::runtime_error{"wait_for_all_sandboxes_done: wait failed"};
	}
	// Sandboxes may now proceed
	client.shm_data->counter.fetch_add(1);
	if (!client.local_data->client_all_sandboxes_done.reset()) {
		throw std::runtime_error{"wait_for_all_sandboxes_done: client_all_sandboxes_done.reset failed"};
	}
	if (!client.local_data->sandboxes_work_begin.reset()) {
		throw std::runtime_error{"wait_for_all_sandboxes_done: sandboxes_work_begin.reset failed"};
	}
	if (!client.local_data->sandboxes_work_finish.set()) {
		throw std::runtime_error{"wait_for_all_sandboxes_done: sandboxes_work_finish.set failed"};
	}
	if (client.shm_data->sandboxes_processing.load(std::memory_order_acquire) > 0) {
		return client_wait_result::not_responding;
	}
	return client_wait_result::done;
}

[[nodiscard]] static
// The sandbox process calls this to wait for a signal from the client that it should begin its processing for the current work unit.
auto wait_for_work_begin(signaling::sandbox sandbox, std::stop_token stop_token, uint64_t* local_counter) -> sandbox_wait_result {
	for (;;) {
		if (!sandbox.local_data->sandboxes_work_begin.wait()) {
			throw std::runtime_error{"wait_for_work_unit: sandboxes_work_begin.wait failed"};
		}
		if (stop_token.stop_requested()) {
			return sandbox_wait_result::stop_requested;
		}
		if (sandbox.shm_data->counter > *local_counter) {
			*local_counter = sandbox.shm_data->counter;
			return sandbox_wait_result::signaled;
		}
		// If we got here, it means another sandbox in the same group had to unblock itself for some reason,
		// which means the event was signaled, so we're just going to loop back and wait again.
	}
}

[[nodiscard]] static
// The sandbox process calls this to wait for a signal that all sandboxes have finished processing the current work unit.
auto wait_for_work_finish(signaling::sandbox sandbox, std::stop_token stop_token, uint64_t* local_counter) -> sandbox_wait_result {
	for (;;) {
		if (!sandbox.local_data->sandboxes_work_finish.wait()) {
			throw std::runtime_error{"wait_for_work_unit: sandboxes_work_finish.wait failed"};
		}
		if (stop_token.stop_requested()) {
			return sandbox_wait_result::stop_requested;
		}
		if (sandbox.shm_data->counter > *local_counter) {
			*local_counter = sandbox.shm_data->counter;
			return sandbox_wait_result::signaled;
		}
		// Same deal as documented above.
	}
}

[[nodiscard]] static
// The sandbox process calls this to notify that it has finished processing.
// If it is the last sandbox to finish processing, the client is notified.
auto notify_sandbox_done(signaling::sandbox sandbox) -> bool {
	const auto prev_value = sandbox.shm_data->sandboxes_processing.fetch_sub(1, std::memory_order_release);
	if (prev_value == 1) {
		// Notify the client that all sandboxes have finished their work.
		return sandbox.local_data->client_all_sandboxes_done.set();
	}
	return true;
}

} // scuff::signaling

#if defined(SCUFF_SIGNALING_MODE_POSIX_SEMAPHORES) ///////////////////////////////////////////

#include <semaphore.h>
#include <sys/stat.h>

namespace scuff::signaling {

struct group_local_data {
	sem_t* signal_client    = nullptr;
	sem_t* signal_sandboxes = nullptr;
	~group_local_data() {
		if (signal_client)    { sem_close(signal_client); }
		if (signal_sandboxes) { sem_close(signal_sandboxes); }
	}
};

[[nodiscard]] static
auto make_client_signal_sem_name(std::string_view group_shmid) -> std::string {
	return std::format("/scuff-signal-client-{}", group_shmid);
}

[[nodiscard]] static
auto make_sandboxes_signal_sem_name(std::string_view group_shmid) -> std::string {
	return std::format("/scuff-signal-sandboxes-{}", group_shmid);
}

static
auto init(signaling::client client, signaling::group_local_data* local_data) -> void {
	const auto signal_client_name    = make_client_signal_sem_name(client.group_shmid);
	const auto signal_sandboxes_name = make_sandboxes_signal_sem_name(client.group_shmid);
	local_data->signal_client        = sem_open(signal_client_name.c_str(), O_CREAT, S_IRUSR | S_IWUSR, 0);
	local_data->signal_sandboxes     = sem_open(signal_sandboxes_name.c_str(), O_CREAT, S_IRUSR | S_IWUSR, 0);
	sem_unlink(signal_client_name.c_str());
	sem_unlink(signal_sandboxes_name.c_str());
}

static
auto init(signaling::sandbox sandbox, signaling::group_local_data* local_data) -> void {
	const auto signal_client_name    = make_client_signal_sem_name(client.group_shmid);
	const auto signal_sandboxes_name = make_sandboxes_signal_sem_name(client.group_shmid);
	local_data->signal_client        = sem_open(signal_client_name.c_str(), 0);
	local_data->signal_sandboxes     = sem_open(signal_sandboxes_name.c_str(), 0);
}

static
auto client_signal_self(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void {
	sem_post(local_data->signal_client);
}

static
auto sandbox_signal_self(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void {
	sem_post(local_data->signal_sandboxes);
}

[[nodiscard]] static
auto signal_sandbox_processing(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data, int sandbox_count, uint64_t epoch) -> bool {
	// Set the sandbox counter
	shm_data->sandboxes_processing.store(sandbox_count);
	// Set the epoch.
	shm_data->epoch.store(epoch);
	// Signal sandboxes to start processing.
	return sem_post(local_data->signal_sandboxes) == 0;
}

[[nodiscard]] static
auto wait_for_all_sandboxes_done(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> wait_for_sandboxes_done_result {
	const auto result = sem_wait(local_data->signal_client);
	if (result != 0) {
		throw std::runtime_error{"wait_for_all_sandboxes_done: sem_wait failed"};
	}
	if (shm_data->sandboxes_processing.load(std::memory_order_acquire) > 0) {
		return wait_for_sandboxes_done_result::not_responding;
	}
	return wait_for_sandboxes_done_result::done;
}

[[nodiscard]] static
auto wait_for_signaled(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data, std::stop_token stop_token, uint64_t* local_epoch) -> wait_for_signaled_result {
	for (;;) {
		const auto result = sem_wait(local_data->signal_sandboxes);
		if (result != WAIT_OBJECT_0) {
			throw std::runtime_error{"wait_for_signaled: WaitForSingleObject failed"};
		}
		if (stop_token.stop_requested()) {
			return wait_for_signaled_result::stop_requested;
		}
		if (shm_data->epoch > *local_epoch) {
			*local_epoch = shm_data->epoch;
			return wait_for_signaled_result::signaled;
		}
		// If we got here, it means another sandbox in the same group didn't receive a heartbeat from the
		// client in time and so it's killing itself. In order to do that it needed to signal itself which
		// also signals all other sandboxes in the group, including this one. So we're just going to loop
		// back and wait again.
	}
}

static
auto notify_sandbox_finished_processing(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void {
	const auto prev_value = shm_data->sandboxes_processing.fetch_sub(1, std::memory_order_release);
	if (prev_value == 1) {
		// Notify the client that all sandboxes have finished their work.
		sem_post(local_data->signal_client);
	}
}

} // scuff::signaling

#endif
