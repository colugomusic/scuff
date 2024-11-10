#pragma once

#include <atomic>
#include <mutex>
#include <stop_token>

// Which signaling mode shall we use? It depends on the platform...
#if defined(_WIN32) ///////////////////////////////////////////////

// On Windows, use SetEvent/WaitForSingleObject
#define SCUFF_SIGNALING_MODE_WIN32_EVENT_OBJECTS

#elif defined(__linux__) //////////////////////////////////////////

// On Linux, use futexes
#define SCUFF_SIGNALING_MODE_LINUX_FUTEXES

#elif defined(__APPLE__) //////////////////////////////////////////

// On macOS, use named Posix semaphores
#define SCUFF_SIGNALING_MODE_POSIX_SEMAPHORES

#endif

namespace scuff::signaling {

struct group_shm_data;
struct group_local_data;
struct client  { group_shm_data* shm_data; std::string_view group_shmid; };
struct sandbox { group_shm_data* shm_data; std::string_view group_shmid; };

enum class wait_for_sandboxes_done_result {
	done,
	not_responding,
};

enum class wait_for_signaled_result {
	signaled,
	stop_requested,
};

struct common_group_shm_data {
	// This is incremented before signaling the
	// sandboxes in the group to process.
	std::atomic<uint64_t> epoch = 0;
	// Each sandbox process decrements this
	// counter when it is finished processing.
	std::atomic<uint32_t> sandboxes_processing;
};

// Initialize the group signalling system for a client.
              static auto init(signaling::client client, signaling::group_local_data* local_data) -> void;

// Initialize the group signalling system for a sandbox.
              static auto init(signaling::sandbox sandbox, signaling::group_local_data* local_data) -> void;

// The client calls this to unblock itself in cases where it is waiting for a signal from
// the sandbox processes but wants to abort the operation (e.g. if one of the sandboxes
// crashed, in which case the signal would never come.)
              static auto client_signal_self(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void;

// The sandbox process calls this to unblock itself in cases where it is waiting for a
// signal from the client but wants to abort the operation (e.g. if the sandbox process
// is shutting down.)
              static auto sandbox_signal_self(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void;

// Signal all sandboxes in the group to begin processing.
[[nodiscard]] static auto signal_sandbox_processing(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data, int sandbox_count, uint64_t epoch) -> bool;

// Wait for all sandboxes in the group to finish processing.
[[nodiscard]] static auto wait_for_all_sandboxes_done(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> wait_for_sandboxes_done_result;

// The sandbox process calls this to wait for a signal from the client that it should begin processing.
[[nodiscard]] static auto wait_for_signaled(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data, std::stop_token stop_token, uint64_t* local_epoch) -> wait_for_signaled_result;

// The sandbox process calls this to notify that it has finished processing.
// If it is the last sandbox to finish processing, the client is notified.
              static auto notify_sandbox_finished_processing(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void;

} // scuff::signaling

#if defined(SCUFF_SIGNALING_MODE_WIN32_EVENT_OBJECTS) //////////////////////////////////////////

#define NOMINMAX
#include <SDKDDKVer.h>
#include <boost/asio.hpp>
#include <string_view>
#include <Windows.h>

namespace scuff::signaling {

struct event_create{ bool manual_reset = false; };
struct event_duplicate{ DWORD client_process_id; HANDLE source_handle; };

[[nodiscard]] static
auto shorten(std::wstring_view s) -> std::string {
	if (s.empty()) {
		return std::string();
	}
	int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), NULL, 0, NULL, NULL);
	std::string buf;
	buf.resize(n);
	WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), &buf[0], n, NULL, NULL);
	return buf;
}

[[nodiscard]] static
auto win32_error_message(int err) -> std::string {
	std::stringstream ss;
	wchar_t buf[1000];
	buf[0] = 0;
	auto size = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 1000, NULL);
	auto ptr = buf + size - 1;
	while (size-- && (*ptr == '\r' || *ptr == '\n')) {
		*ptr-- = '\0';
	}
	ss << shorten(buf);
	ss << " [" << err << "]";
	return ss.str();
}

struct win32_event {
	win32_event() = default;
	win32_event(event_create c)
		: h{CreateEventA(0, c.manual_reset, 0, 0)}
	{
		if (!h) {
			throw std::runtime_error{std::format("CreateEvent failed: '{}'", win32_error_message(GetLastError()))};
		}
	}
	win32_event(event_duplicate d) {
		p = OpenProcess(PROCESS_DUP_HANDLE, FALSE, d.client_process_id);
		if (DuplicateHandle(p, d.source_handle, GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS) == 0) {
			//while (!IsDebuggerPresent()) { Sleep(100); } __debugbreak();
			throw std::runtime_error{std::format("DuplicateHandle failed: '{}'", win32_error_message(GetLastError()))};
		}
	}
	win32_event(win32_event&& other) noexcept
		: h{other.h}
		, p{other.p}
	{
		other.h = 0;
		other.p = 0;
	}
	win32_event& operator=(win32_event&& other) noexcept {
		if (h) { CloseHandle(h); }
		if (p) { CloseHandle(p); }
		h = other.h;
		p = other.p;
		other.h = 0;
		other.p = 0;
		return *this;
	}
	~win32_event() {
		if (h) { CloseHandle(h); }
		if (p) { CloseHandle(p); }
	}
	HANDLE h = 0;
	HANDLE p = 0;
};

struct group_shm_data : common_group_shm_data {
	// Client process creates these handles.
	// Sandbox processes need to duplicate them in order to use them.
	DWORD client_process_id = 0;
	HANDLE signal_client    = nullptr;
	HANDLE signal_sandboxes = nullptr;
};

struct group_local_data {
	win32_event signal_sandboxes_event;
	win32_event signal_client_event;
};

static
auto init(signaling::client client, signaling::group_local_data* local_data) -> void {
	local_data->signal_sandboxes_event = win32_event{event_create{false}};
	local_data->signal_client_event    = win32_event{event_create{false}};
	client.shm_data->client_process_id = GetCurrentProcessId();
	client.shm_data->signal_sandboxes  = local_data->signal_sandboxes_event.h;
	client.shm_data->signal_client     = local_data->signal_client_event.h;
}

static
auto init(signaling::sandbox sandbox, signaling::group_local_data* local_data) -> void {
	local_data->signal_sandboxes_event = win32_event{event_duplicate{sandbox.shm_data->client_process_id, sandbox.shm_data->signal_sandboxes}};
	local_data->signal_client_event    = win32_event{event_duplicate{sandbox.shm_data->client_process_id, sandbox.shm_data->signal_client}};
}

static
auto client_signal_self(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void {
	SetEvent(local_data->signal_client_event.h);
}

static
auto sandbox_signal_self(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void {
	SetEvent(local_data->signal_sandboxes_event.h);
}

[[nodiscard]] static
auto signal_sandbox_processing(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data, int sandbox_count, uint64_t epoch) -> bool {
	// Set the sandbox counter
	shm_data->sandboxes_processing.store(sandbox_count);
	// Set the epoch.
	shm_data->epoch.store(epoch);
	// Signal sandboxes to start processing.
	return SetEvent(local_data->signal_sandboxes_event.h);
}

[[nodiscard]] static
auto wait_for_all_sandboxes_done(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> wait_for_sandboxes_done_result {
	const auto result = WaitForSingleObject(local_data->signal_client_event.h, INFINITE);
	if (result != WAIT_OBJECT_0) {
		throw std::runtime_error{"wait_for_all_sandboxes_done: WaitForSingleObject failed"};
	}
	if (shm_data->sandboxes_processing.load(std::memory_order_acquire) > 0) {
		return wait_for_sandboxes_done_result::not_responding;
	}
	return wait_for_sandboxes_done_result::done;
}

[[nodiscard]] static
auto wait_for_signaled(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data, std::stop_token stop_token, uint64_t* local_epoch) -> wait_for_signaled_result {
	for (;;) {
		// TOODOO: refactor is needed here because we need to create separate events for each sandbox.
		const auto result = WaitForSingleObject(local_data->signal_sandboxes_event.h, INFINITE);
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
		SetEvent(local_data->signal_client_event.h);
	}
}

} // scuff::signaling

#elif defined(SCUFF_SIGNALING_MODE_LINUX_FUTEXES) //////////////////////////////////////////////

#include <sys/syscall.h>
#include <linux/fuxex.h>

namespace scuff::signaling {

struct group_local_data {
	std::atomic<uint32_t> signal_client;
	std::atomic<uint32_t> signal_sandboxes;
};

struct group_shm_data : common_group_shm_data {};

static
auto futex_wait(std::atomic<uint32_t>* uaddr, uint32_t expected_value) -> void {
	syscall(SYS_futex, uaddr, FUTEX_WAIT, expected_value, nullptr, nullptr, 0);
}

static
auto futex_wake_one(std::atomic<uint32_t>* uaddr) -> void {
	syscall(SYS_futex, uaddr, FUTEX_WAKE, 1, nullptr, nullptr, 0);
}

static
auto futex_wake_all(std::atomic<uint32_t>* uaddr) -> void {
	syscall(SYS_futex, uaddr, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

static
auto init(signaling::client client, signaling::group_local_data* local_data) -> void {
	// Nothing to do
}

static
auto init(signaling::sandbox sandbox, signaling::group_local_data* local_data) -> void {
	// Nothing to do
}

static
auto client_signal_self(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void {
	local_data->signal_client.store(1, std::memory_order_release);
	futex_wake_all(&local_data->signal_client);
}

static
auto sandbox_signal_self(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> void {
	local_data->signal_sandboxes.store(1, std::memory_order_release);
	futex_wake_all(&local_data->signal_sandboxes);
}

[[nodiscard]] static
auto signal_sandbox_processing(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data, int sandbox_count, uint64_t epoch) -> bool {
	// Set the sandbox counter
	shm_data->sandboxes_processing.store(sandbox_count);
	// Set the epoch
	shm_data->epoch.store(epoch);
	// Signal sandboxes to start processing.
	futex_wake_all(&local_data->signal_sandboxes);
	return true;
}

[[nodiscard]] static
auto wait_for_all_sandboxes_done(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data) -> wait_for_sandboxes_done_result {
	futex_wait(&local_data->signal_client, 0);
	local_data->signal_client.store(0, std::memory_order_release);
	if (shm_data->sandboxes_processing.load(std::memory_order_acquire) > 0) {
		return wait_for_sandboxes_done_result::not_responding;
	}
	return wait_for_sandboxes_done_result::done;
}

[[nodiscard]] static
auto wait_for_signaled(signaling::group_shm_data* shm_data, signaling::group_local_data* local_data, std::stop_token stop_token, uint64_t* local_epoch) -> wait_for_signaled_result {
	for (;;) {
		futex_wait(&local_data->signal_sandboxes, 0);
		// TOODOO: this doesn't make sense because it would break if there's more than one sandbox.
		local_data->signal_sandboxes.store(0, std::memory_order_release);
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
		futex_wake_all(&local_data->signal_client);
	}
}

} // scuff::signaling

#elif defined(SCUFF_SIGNALING_MODE_POSIX_SEMAPHORES) ///////////////////////////////////////////

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

struct group_shm_data : common_group_shm_data {};

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

#elif defined(SCUFF_SIGNALING_MODE_BOOST_IPC) //////////////////////////////////////////////////
// This is the easiest to understand implementation but the performance is bad.
// boost::interprocess_condition uses CreateSemaphore and WaitForSingleObject under the hood.
// On macOS and Linux it uses sem_wait and sem_post.
// It may also be doing other bad stuff which slows things down.

#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

namespace bip = boost::interprocess;

namespace scuff::signaling {

struct group_local_data {
	bip::interprocess_mutex mut;
	bip::interprocess_condition cv;
};

static
auto init(signaling::client client, signaling::group_local_data* local_data) -> void {
	// Nothing to do
}

static
auto init(signaling::sandbox sandbox, signaling::group_local_data* local_data) -> void {
	// Nothing to do
}

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

// Busy-waiting with yield(), good performance but very high CPU usage so unusable.

namespace scuff::signaling {

struct group_local_data {
};

static
auto init(signaling::client client, signaling::group_local_data* local_data) -> void {
	// Nothing to do
}

static
auto init(signaling::sandbox sandbox, signaling::group_local_data* local_data) -> void {
	// Nothing to do
}

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
