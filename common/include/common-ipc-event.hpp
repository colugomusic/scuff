#pragma once

#if defined(_WIN32) ////////////////////////////////////////////////////////////////////

#define NOMINMAX
#include <SDKDDKVer.h>
#include <boost/asio.hpp> // Included to resolve header conflicts
#include <format>
#include <string_view>
#include <Windows.h>

namespace scuff::ipc {

struct shared_event {
	DWORD process;
	HANDLE handle;
};

struct local_event_create { shared_event* shared; };
struct local_event_open { shared_event* shared; };

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
	wchar_t buf[1000];
	buf[0] = 0;
	auto size = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 1000, NULL);
	auto ptr = buf + size - 1;
	while (size-- && (*ptr == '\r' || *ptr == '\n')) {
		*ptr-- = '\0';
	}
	return std::format("{} [{}]", shorten(buf), err);
}

static
auto create_shared_event(shared_event* e) -> void {
	if (const auto handle = CreateEventA(0, TRUE, 0, 0)) {
		*e = {GetCurrentProcessId(), handle};
		return;
	}
	throw std::runtime_error{std::format("CreateEvent failed: '{}'", win32_error_message(GetLastError()))};
}

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
auto init(local_event_impl* impl, local_event_create create) -> void {
	impl->event = win32_local_event{create};
} 

static
auto init(local_event_impl* impl, local_event_open open) -> void {
	impl->event = win32_local_event{open};
} 

[[nodiscard]] static
auto set(const local_event_impl* impl) -> bool {
	return SetEvent(impl->event.h);
} 

[[nodiscard]] static
auto wait(const local_event_impl* impl) -> bool        {
	if (WaitForSingleObject(impl->event.h, INFINITE) != WAIT_OBJECT_0) {
		return false;
	}
	if (!ResetEvent(impl->event.h)) {
		return false;
	}
	return true;
} 

} // scuff::ipc

#elif define(__linux__) ////////////////////////////////////////////////////////////////

#include <sys/syscall.h>
#include <linux/futex.h>

namespace scuff::ipc {

struct shared_event {
	std::atomic<uint32_t> word;
};

struct local_event_create{ shared_event* shared };
struct local_event_open{ shared_event* shared };

static
auto create_shared_event(shared_event* e) -> void {
	// Nothing to do.
}

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

static auto init(local_event_impl* impl, local_event_create create) -> void { impl->shared = create.shared; } 
static auto init(local_event_impl* impl, local_event_open open) -> void     { impl->shared = open.shared; } 

//[[nodiscard]] static
//auto reset(const local_event_impl* impl) -> bool {
//	impl->shared->word.store(0, std::memory_order_release);
//	return true;
//} 

[[nodiscard]] static
auto set(const local_event_impl* impl) -> bool {
	impl->shared->word.store(1, std::memory_order_release);
	futex_wake_all(impl->word);
	return true;
} 

[[nodiscard]] static
auto wait(const local_event_impl* impl) -> bool {
	futex_wait(impl->word, 0);
	impl->shared->word.store(0, std::memory_order_release);
	return true;
} 

} // scuff::ipc

#elif defined(__APPLE__) ///////////////////////////////////////////////////////////////

namespace scuff::ipc {

} // scuff::ipc

#endif

namespace scuff::ipc {

struct local_event {
	local_event() = default;
	local_event(local_event_create create)  { ipc::init(&impl, create); }
	local_event(local_event_open open)      { ipc::init(&impl, open); }
	auto set() const -> bool                { return ipc::set(&impl); }
	[[nodiscard]] auto wait() const -> bool { return ipc::wait(&impl); }
private:
	local_event_impl impl;
};

} // scuff::ipc