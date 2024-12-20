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
	ipc::init(ipc::shared_event_create{&init.group.shm->all_sandboxes_done, init.group_shmid});
	init.group.local->all_sandboxes_done = ipc::local_event{ipc::local_event_create{&init.group.shm->all_sandboxes_done}};
}

static
// Initialize client-side sandbox signalling
auto init(signaling::clientside_sandbox_init init) -> void {
	ipc::init(ipc::shared_event_create{&init.sandbox.shm->work_begin, init.sbox_shmid});
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
auto unblock_self(signaling::clientside_group group) -> void {
	group.local->all_sandboxes_done.set();
}

static
// The sandbox process calls this to unblock itself in cases where it is waiting for a
// signal from the client but wants to abort the operation (e.g. if the sandbox process
// is shutting down.)
auto unblock_self(signaling::sandboxside_sandbox sandbox) -> void {
	sandbox.local->work_begin.set();
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
	group.local->all_sandboxes_done.wait();
	if (group.shm->sandboxes_processing.load(std::memory_order_acquire) > 0) {
		return client_wait_result::not_responding;
	}
	return client_wait_result::done;
}

[[nodiscard]] static
// The sandbox process calls this to wait for a signal from the client that it should begin its processing.
auto wait_for_work_begin(signaling::sandboxside_sandbox sandbox, std::stop_token stop_token) -> sandbox_wait_result {
	sandbox.local->work_begin.wait();
	if (stop_token.stop_requested()) {
		return sandbox_wait_result::stop_requested;
	}
	return sandbox_wait_result::signaled;
}

static
// The sandbox process calls this to notify that it has finished processing.
// If it is the last sandbox to finish processing, the client is notified.
auto notify_sandbox_done(signaling::sandboxside_group group) -> void {
	const auto prev_value = group.shm->sandboxes_processing.fetch_sub(1, std::memory_order_release);
	if (prev_value == 1) {
		// Notify the client that all sandboxes have finished their work.
		group.local->all_sandboxes_done.set();
	}
}

} // scuff::signaling
