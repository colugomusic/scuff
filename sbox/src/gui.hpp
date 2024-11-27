#pragma once

#include "clap.hpp"
#include "data.hpp"
#include "os.hpp"

namespace scuff::sbox::gui {

[[nodiscard]] static
auto create_gui(sbox::app* app, const sbox::device& dev) -> sbox::create_gui_result {
	switch (dev.type) {
		case plugin_type::clap: { return clap::main::create_gui(app, dev); }
		case plugin_type::vst3: { /* Not implemented yet. */ return {}; }
		default:                { assert (false); return {}; }
	}
}

[[nodiscard]] static
auto setup_editor_window(sbox::app* app, const sbox::device& dev) -> bool {
	os::setup_editor_window(app, dev);
	switch (dev.type) {
		case plugin_type::clap: { return clap::main::setup_editor_window(app, dev); }
		case plugin_type::vst3: { /* Not implemented yet. */ return false; }
		default:                { assert (false); return false; }
	}
}

static
auto hide(sbox::app* app, sbox::device dev) -> void {
	if (!dev.ui.window) {
		return;
	}
	os::shutdown_editor_window(app, dev);
	switch (dev.type) {
		case plugin_type::clap: { clap::main::shutdown_editor_window(app, dev); break; }
		case plugin_type::vst3: { /* Not implemented yet. */ break; }
	}
	window_hide(dev.ui.window);
	app->msg_sender.enqueue(scuff::msg::out::device_editor_visible_changed{dev.id.value, false, (int64_t)(os::get_editor_window_native_handle(dev))});
	app->model.update(ez::main, [dev](model&& m){
		m.devices = m.devices.insert(dev);
		return m;
	});
}

static
auto on_native_window_resize_impl(sbox::app* app, const sbox::device& dev, window_size_f window_size) -> void {
	switch (dev.type) {
		case plugin_type::clap: { clap::main::on_native_window_resize(app, dev, window_size); break; }
		case plugin_type::vst3: { /* Not implemented yet. */ break; }
		default:                { assert (false); break; }
	}
}

static
auto on_native_window_close(device_window_listener* dwl, Event* event) -> void {
	const auto& device = dwl->app->model.read(ez::main).devices.at(dwl->dev_id);
	gui::hide(dwl->app, device);
}

static
auto on_native_window_resize(device_window_listener* dwl, Event* event) -> void {
	const auto size = event_params(event, EvSize);
	log_printf("on_window_resize: %f,%f", size->width, size->height);
	const auto m    = dwl->app->model.read(ez::main);
	const auto& dev = m.devices.at(dwl->dev_id);
	on_native_window_resize_impl(dwl->app, dev, {size->width, size->height});
}

static
auto show(sbox::app* app, scuff::id::device dev_id) -> void {
	const auto devices   = app->model.read(ez::main).devices;
	auto device          = devices.at({dev_id});
	const auto has_gui   = device.service->shm.data->flags.value & shm::device_flags::has_gui;
	if (!has_gui) {
		log(app, "Device %d has no GUI", device.id.value);
		return;
	}
	if (device.ui.window) {
		window_destroy(&device.ui.window);
	}
	const auto result = create_gui(app, device);
	if (!result.success) {
		log(app, "Failed to create GUI for device %d", device.id.value);
		return;
	}
	uint32_t window_flags = ekWINDOW_STDRES;
	if (!result.resizable) {
		window_flags &= ~ekWINDOW_RESIZE;
	}
	window_flags &= ~ekWINDOW_MAX;
	window_flags &= ~ekWINDOW_MIN;
	device.ui.window = window_create(window_flags);
	device.ui.panel  = panel_create();
	device.ui.layout = layout_create(1, 1);
	device.ui.view   = view_create();
	layout_view(device.ui.layout, device.ui.view, 0, 0);
	if (!result.resizable) {
		view_size(device.ui.view, S2Df(float(result.width), float(result.height)));
	}
	panel_layout(device.ui.panel, device.ui.layout);
	window_panel(device.ui.window, device.ui.panel);
	window_OnClose(device.ui.window, listener(&device.service->window_listener, on_native_window_close, device_window_listener));
	window_OnResize(device.ui.window, listener(&device.service->window_listener, on_native_window_resize, device_window_listener));
	if (result.resizable) {
		window_size(device.ui.window, S2Df(float(result.width), float(result.height)));
	}
	window_show(device.ui.window);
	app->msg_sender.enqueue(scuff::msg::out::device_editor_visible_changed{device.id.value, false, (int64_t)(os::get_editor_window_native_handle(device))});
	if (!setup_editor_window(app, device)) {
		log(app, "Failed to setup clap editor window");
	}
	app->model.update(ez::main, [device](model&& m){
		m.devices = m.devices.insert(device);
		return m;
	});
}

} // scuff::sbox::gui
