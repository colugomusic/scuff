#define NOMINMAX
#include "window.hpp"
#include <Windows.h>

namespace scuff::sbox::ezwin {

static constexpr auto MIN_SIZE = 10;

struct window {
	HWND hwnd = nullptr;
	fn::on_window_closed on_closed;
	fn::on_window_resized on_resized;
};

static
auto get_window(HWND hwnd) -> window* {
	return (window*)(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

static
auto wm_close(HWND hwnd, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
	if (const auto wnd = get_window(hwnd)) {
		if (wnd->on_closed) {
			wnd->on_closed();
		}
		destroy(wnd);
	}
	return 0;
}

static
auto wm_create(HWND hwnd, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
	auto* const create_params = (CREATESTRUCT*)(l);
	SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)(create_params->lpCreateParams));
	return 0;
}

static
auto wm_destroy(HWND hwnd, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
	if (const auto wnd = get_window(hwnd)) {
		delete wnd;
	}
	return 0;
}

static
auto wm_size(HWND hwnd, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
	if (const auto wnd = get_window(hwnd)) {
		if (wnd->on_resized) {
			const auto width  = LOWORD(l);
			const auto height = HIWORD(l);
			wnd->on_resized(size{width, height});
		}
	}
	return 0;
}

static
auto CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) -> LRESULT {
	switch (msg) {
		case WM_CLOSE:   { return wm_close(hwnd, msg, w, l); }
		case WM_CREATE:  { return wm_create(hwnd, msg, w, l); }
		case WM_DESTROY: { return wm_destroy(hwnd, msg, w, l); }
		case WM_SIZE:    { return wm_size(hwnd, msg, w, l); }
	}
	return DefWindowProc(hwnd, msg, w, l);
}

static
auto init_wndclass() -> WNDCLASS {
	const auto wndclass = WNDCLASS{
		.lpfnWndProc = wndproc,
		.hInstance = GetModuleHandle(nullptr),
		.lpszClassName = "ezwin",
	};
	RegisterClass(&wndclass);
	return wndclass;
}

static
auto get_wndclass() -> std::string_view {
	static const auto wndclass = init_wndclass();
	return wndclass.lpszClassName;
}

[[nodiscard]] static
auto make_style(ezwin::resizable resizable) -> DWORD {
	auto style = WS_OVERLAPPEDWINDOW;
	if (!resizable.value) {
		style &= ~WS_SIZEBOX;
	}
	return style;
}

auto create(window_config cfg) -> window* {
	auto wnd = std::make_unique<window>();
	const auto exstyle = DWORD{0};
	const auto wndclass = get_wndclass();
	const auto title = cfg.title.value;
	const auto style = make_style(cfg.resizable);
	const auto x = std::max(0, cfg.position.x);
	const auto y = std::max(0, cfg.position.y);
	const auto w = std::max(MIN_SIZE, cfg.size.width);
	const auto h = std::max(MIN_SIZE, cfg.size.height);
	const auto parent = (HWND)(cfg.parent.value);
	const auto menu = (HMENU)(0);
	const auto hinstance = GetModuleHandleA(0);
	const auto create_params = wnd.get();
	wnd->hwnd = CreateWindowEx(exstyle, wndclass.data(), title.data(), style, x, y, w, h, parent, menu, hinstance, create_params);
	if (!wnd->hwnd) {
		return nullptr;
	}
	set(wnd.get(), cfg.on_closed);
	set(wnd.get(), cfg.on_resized);
	set(wnd.get(), cfg.icon);
	set(wnd.get(), cfg.visible);
	return wnd.release();
}

auto destroy(window* wnd) -> void {
	if (!wnd)       { return; }
	if (!wnd->hwnd) { return; }
	DestroyWindow(wnd->hwnd);
}

auto get_native_handle(const window& wnd) -> native_handle {
	return native_handle{wnd.hwnd};
}

auto set(window* wnd, ezwin::icon32 icon) -> void {
	// TOODOO:
}

auto set(window* wnd, ezwin::position position) -> void {
	SetWindowPos(wnd->hwnd, nullptr, position.x, position.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

auto set(window* wnd, ezwin::position position, ezwin::size size) -> void {
	SetWindowPos(wnd->hwnd, nullptr, position.x, position.y, size.width, size.height, SWP_NOZORDER);
}

auto set(window* wnd, ezwin::resizable resizable) -> void {
	auto style = GetWindowLongPtr(wnd->hwnd, GWL_STYLE);
	style = resizable.value ? (style | WS_SIZEBOX) : (style & ~WS_SIZEBOX);
	SetWindowLongPtr(wnd->hwnd, GWL_STYLE, style);
	SetWindowPos(wnd->hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
}

auto set(window* wnd, ezwin::size size) -> void {
	SetWindowPos(wnd->hwnd, nullptr, 0, 0, size.width, size.height, SWP_NOMOVE | SWP_NOZORDER);
}

auto set(window* wnd, ezwin::title title) -> void {
	SetWindowText(wnd->hwnd, title.value.data());
}

auto set(window* wnd, ezwin::visible visible) -> void {
	ShowWindow(wnd->hwnd, visible.value ? SW_SHOW : SW_HIDE);
}

auto set(window* wnd, fn::on_window_closed cb) -> void {
	wnd->on_closed = cb;
}

auto set(window* wnd, fn::on_window_resized cb) -> void {
	wnd->on_resized = cb;
}

auto process_messages() -> void {
	auto msg = MSG{};
	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

} // scuff::sbox::ezwin