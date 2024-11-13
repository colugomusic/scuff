#pragma once

#include "common-ipc-event.hpp"
#include <atomic>
#include <format>
#include <mutex>
#include <stop_token>

namespace scuff::signaling {

struct group_local_data;
struct group_shm_data;
struct sandbox_local_data;
struct sandbox_shm_data;
struct clientside_group         { group_local_data* local; group_shm_data* shm; };
struct clientside_group_init    { std::string_view group_shmid; signaling::clientside_group group; };
struct clientside_sandbox       { sandbox_local_data* local; sandbox_shm_data* shm; };
struct clientside_sandbox_init  { std::string_view sbox_shmid; signaling::clientside_sandbox sandbox; };
struct sandboxside_group        { group_local_data* local; group_shm_data* shm; };
struct sandboxside_group_init   { std::string_view group_shmid; signaling::sandboxside_group group; };
struct sandboxside_sandbox      { sandbox_local_data* local; sandbox_shm_data* shm; };
struct sandboxside_sandbox_init { std::string_view sbox_shmid; signaling::sandboxside_sandbox sandbox; };

enum class client_wait_result {
	done,
	not_responding,
};

enum class sandbox_wait_result {
	signaled,
	stop_requested,
};

struct group_local_data {
	ipc::local_event all_sandboxes_done;
};

struct group_shm_data {
	// Each sandbox process decrements this
	// counter when it is finished processing.
	std::atomic<uint32_t> sandboxes_processing;
	// The last sandbox to finish processing signals this.
	ipc::shared_event all_sandboxes_done;
};

struct sandbox_local_data {
	ipc::local_event work_begin;
};

struct sandbox_shm_data {
	ipc::shared_event work_begin;
};

static
// Initialize client-side group signalling
auto init(signaling::clientside_group_init init) -> void {
	const auto name = std::format("scuff-signal-group-{}", init.group_shmid);
	ipc::init(ipc::shared_event_create{&init.group.shm->all_sandboxes_done, name});
	init.group.local->all_sandboxes_done = ipc::local_event{ipc::local_event_create{&init.group.shm->all_sandboxes_done}};
}

static
// Initialize client-side sandbox signalling
auto init(signaling::clientside_sandbox_init init) -> void {
	const auto name = std::format("scuff-signal-sandbox-{}", init.sbox_shmid);
	ipc::init(ipc::shared_event_create{&init.sandbox.shm->work_begin, name});
	init.sandbox.local->work_begin = ipc::local_event{ipc::local_event_create{&init.sandbox.shm->work_begin}};
}

static
// Initialize sandbox-side group signalling
auto init(signaling::sandboxside_group_init init) -> void {
	init.group.local->all_sandboxes_done = ipc::local_event{ipc::local_event_open{&init.group.shm->all_sandboxes_done}};
}

static
// Initialize sandbox-side sandbox signalling
auto init(signaling::sandboxside_sandbox_init init) -> void {
	init.sandbox.local->work_begin = ipc::local_event{ipc::local_event_open{&init.sandbox.shm->work_begin}};
}

static
// The client calls this to unblock itself in cases where it is waiting for a signal from
// the sandbox processes but wants to abort the operation (e.g. if one of the sandboxes
// crashed, in which case the signal would never come.)
auto unblock_self(signaling::clientside_group group) -> bool {
	return group.local->all_sandboxes_done.set();
}

static
// The sandbox process calls this to unblock itself in cases where it is waiting for a
// signal from the client but wants to abort the operation (e.g. if the sandbox process
// is shutting down.)
auto unblock_self(signaling::sandboxside_sandbox sandbox) -> bool {
	return sandbox.local->work_begin.set();
}

[[nodiscard]] static
// Signal all sandboxes in the group to begin processing.
auto sandboxes_work_begin(signaling::clientside_group group, int sandbox_count, auto next_sandbox_signal) -> bool {
	group.shm->sandboxes_processing.store(sandbox_count);
	for (int i = 0; i < sandbox_count; ++i) {
		next_sandbox_signal().set();
	}
	return true;
}

[[nodiscard]] static
// Wait for all sandboxes in the group to finish processing.
auto wait_for_all_sandboxes_done(signaling::clientside_group group) -> client_wait_result {
	if (!group.local->all_sandboxes_done.wait()) {
		throw std::runtime_error{"all_sandboxes_done.wait failed"};
	}
	if (group.shm->sandboxes_processing.load(std::memory_order_acquire) > 0) {
		return client_wait_result::not_responding;
	}
	return client_wait_result::done;
}

[[nodiscard]] static
// The sandbox process calls this to wait for a signal from the client that it should begin its processing.
auto wait_for_work_begin(signaling::sandboxside_sandbox sandbox, std::stop_token stop_token) -> sandbox_wait_result {
	if (!sandbox.local->work_begin.wait()) {
		throw std::runtime_error{"work_begin.wait failed"};
	}
	if (stop_token.stop_requested()) {
		return sandbox_wait_result::stop_requested;
	}
	return sandbox_wait_result::signaled;
}

[[nodiscard]] static
// The sandbox process calls this to notify that it has finished processing.
// If it is the last sandbox to finish processing, the client is notified.
auto notify_sandbox_done(signaling::sandboxside_group group) -> bool {
	const auto prev_value = group.shm->sandboxes_processing.fetch_sub(1, std::memory_order_release);
	if (prev_value == 1) {
		// Notify the client that all sandboxes have finished their work.
		return group.local->all_sandboxes_done.set();
	}
	return true;
}

} // scuff::signaling

#if defined(SCUFF_SIGNALING_MODE_POSIX_SEMAPHORES) ///////////////////////////////////////////

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
