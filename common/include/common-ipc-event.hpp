#pragma once

#include <string_view>

// Cross-platform IPC signaling.
// Assumes that at most one thread will be waiting on an event at any time.
// Events are reset by the waiting thread after being signaled.

// scuff::ipc::shared_event can be created in shared memory, allowing other
// processes to create a scuff::ipc::local_event from it.

// scuff::ipc::local_event is the process's local view of the event.

// Implementation:
//  - Windows: Event objects
//  - Linux:   Futexes
//  - macOS:   POSIX Semaphores

namespace scuff::ipc {

struct shared_event;

struct local_event_create { shared_event* shared; };
struct local_event_open   { shared_event* shared; };

// Name is only required for macOS.
struct shared_event_create { shared_event* shared; std::string_view name; };

} // scuff::ipc

#if defined(_WIN32) ////////////////////////////////////////////////////////////////////

#define NOMINMAX
#include <SDKDDKVer.h>
#include <boost/asio.hpp> // Included to resolve header conflicts
#include <format>
#include <Windows.h>
#include <cwctype>

namespace scuff::ipc::detail {

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
	auto buf      = std::array<wchar_t, 1000>{};
	auto buf_size = static_cast<DWORD>(std::size(buf));
	buf[0] = 0;
	auto size = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf.data(), buf_size, NULL);
	// Removing trailing whitespace characters
	for (auto pos = buf.rbegin(); pos != buf.rend() && std::iswspace(*pos); pos++) {
		*pos = '\0';
	}
	return std::format("{} [{}]", shorten(buf.data()), err);
}

} // scuff::ipc::detail

namespace scuff::ipc {

struct shared_event {
	DWORD process;
	HANDLE handle;
};

static
auto init(shared_event_create create) -> void {
	if (const auto handle = CreateEventA(0, TRUE, 0, 0)) {
		*create.shared = {GetCurrentProcessId(), handle};
		return;
	}
	throw std::runtime_error{std::format("CreateEvent failed: '{}'", detail::win32_error_message(GetLastError()))};
}

} // scuff::ipc

namespace scuff::ipc::detail {

struct win32_local_event {
	win32_local_event() = default;
	win32_local_event(local_event_create c)
		: h{c.shared->handle}
	{
	}
	win32_local_event(local_event_open o) {
		p = OpenProcess(PROCESS_DUP_HANDLE, FALSE, o.shared->process);
		if (DuplicateHandle(p, o.shared->handle, GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS) == 0) {
			throw std::runtime_error{std::format("DuplicateHandle failed: '{}'", win32_error_message(GetLastError()))};
		}
	}
	win32_local_event(win32_local_event&& other) noexcept
		: h{other.h}
		, p{other.p}
	{
		other.h = 0;
		other.p = 0;
	}
	win32_local_event& operator=(win32_local_event&& other) noexcept {
		if (h) { CloseHandle(h); }
		if (p) { CloseHandle(p); }
		h = other.h;
		p = other.p;
		other.h = 0;
		other.p = 0;
		return *this;
	}
	~win32_local_event() {
		if (h) { CloseHandle(h); }
		if (p) { CloseHandle(p); }
	}
	HANDLE h = 0;
	HANDLE p = 0;
};

struct local_event_impl {
	win32_local_event event;
};

static
auto set(const local_event_impl* impl) -> void {
	if (!SetEvent(impl->event.h)) {
		auto msg = std::format("SetEvent failed: '{}'", win32_error_message(GetLastError()));
		throw std::runtime_error{msg};
	}
} 

static
auto wait(const local_event_impl* impl) -> void {
	if (WaitForSingleObject(impl->event.h, INFINITE) != WAIT_OBJECT_0) {
		auto msg = std::format("WaitForSingleObject failed");
		throw std::runtime_error{msg};
	}
	if (!ResetEvent(impl->event.h)) {
		auto msg = std::format("ResetEvent failed: '{}'", win32_error_message(GetLastError()));
		throw std::runtime_error{msg};
	}
} 

static auto init(local_event_impl* impl, local_event_create create) -> void { impl->event = win32_local_event{create}; } 
static auto init(local_event_impl* impl, local_event_open open) -> void     { impl->event = win32_local_event{open}; } 

} // scuff::ipc::detail

#elif defined(__linux__) ///////////////////////////////////////////////////////////////

#include <sys/syscall.h>
#include <linux/futex.h>

namespace scuff::ipc {

struct shared_event {
	std::atomic<uint32_t> word;
};

static auto init(shared_event_create create) -> void {} 

} // scuff::ipc

namespace scuff::ipc::detail {

struct local_event_impl {
	shared_event* shared;
};

static
auto futex_wait(std::atomic<uint32_t>* word, uint32_t expected_value) -> void {
	syscall(SYS_futex, word, FUTEX_WAIT, expected_value, nullptr, nullptr, 0);
}

static
auto futex_wake_all(std::atomic<uint32_t>* word) -> void {
	syscall(SYS_futex, word, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
}

static
auto set(const local_event_impl* impl) -> void {
	impl->shared->word.store(1, std::memory_order_release);
	futex_wake_all(&impl->shared->word);
} 

static
auto wait(const local_event_impl* impl) -> void {
	futex_wait(&impl->shared->word, 0);
	impl->shared->word.store(0, std::memory_order_release);
} 

static auto init(local_event_impl* impl, local_event_create create) -> void { impl->shared = create.shared; } 
static auto init(local_event_impl* impl, local_event_open open) -> void     { impl->shared = open.shared; } 

} // scuff::ipc::detail

#elif defined(__APPLE__) ///////////////////////////////////////////////////////////////

#include <cerrno>
#include <cstring>
#include <format>
#include <semaphore.h>

namespace scuff::ipc {

struct shared_event {
	char name[100];
};

static
auto init(shared_event_create create) -> void {
	const auto name = std::format("/{}", create.name);
	std::strncpy(create.shared->name, name.c_str(), sizeof(create.shared->name));
}

} // scuff::ipc

namespace scuff::ipc::detail {

struct posix_local_event {
	posix_local_event() = default;
	posix_local_event(local_event_create c)
		: sem{sem_open(c.shared->name, O_CREAT, S_IRUSR | S_IWUSR, 0)}
	{
		if (sem == SEM_FAILED) {
			throw std::runtime_error{std::format("sem_open('{}', O_CREAT) failed: '{}'", std::string_view{c.shared->name}, std::strerror(errno))};
		}
	}
	posix_local_event(local_event_open o)
		: sem{sem_open(o.shared->name, 0)}
	{
		if (sem == SEM_FAILED) {
			throw std::runtime_error{std::format("sem_open('{}') failed: '{}'", std::string_view{o.shared->name}, std::strerror(errno))};
		}
		sem_unlink(o.shared->name);
	}
	posix_local_event(posix_local_event&& other) noexcept
		: sem{other.sem}
	{
		other.sem = nullptr;
	}
	posix_local_event& operator=(posix_local_event&& other) noexcept {
		if (sem) { sem_close(sem); }
		sem = other.sem;
		other.sem = nullptr;
		return *this;
	}
	~posix_local_event() {
		if (sem) { sem_close(sem); }
	}
	sem_t* sem = nullptr;
};

struct local_event_impl {
	posix_local_event event;
};

static
auto set(const local_event_impl* impl) -> void {
	if (sem_post(impl->event.sem) != 0) {
		auto msg = std::format("sem_post failed: '{}'", std::strerror(errno));
		throw std::runtime_error{msg};
	}
} 

static
auto wait(const local_event_impl* impl) -> void {
	if (sem_wait(impl->event.sem) != 0) {
		auto msg = std::format("sem_wait failed: '{}'", std::strerror(errno));
		throw std::runtime_error{msg};
	}
} 

static auto init(local_event_impl* impl, local_event_create create) -> void { impl->event = posix_local_event{create}; } 
static auto init(local_event_impl* impl, local_event_open open) -> void     { impl->event = posix_local_event{open}; } 

} // scuff::ipc::detail

#endif

namespace scuff::ipc {

struct local_event {
	local_event() = default;
	local_event(local_event_create create)  { ipc::detail::init(&impl, create); }
	local_event(local_event_open open)      { ipc::detail::init(&impl, open); }
	auto set() const -> void                { ipc::detail::set(&impl); }
	auto wait() const -> void { ipc::detail::wait(&impl); }
private:
	detail::local_event_impl impl;
};

} // scuff::ipc
