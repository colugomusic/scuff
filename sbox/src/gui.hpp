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
	ezwin::set(dev.ui.window, ezwin::visible{false});
	app->msg_sender.enqueue(scuff::msg::out::device_editor_visible_changed{dev.id.value, false, (int64_t)(ezwin::get_native_handle(*dev.ui.window).value)});
	app->model.update(ez::main, [dev](model&& m){
		m.devices = m.devices.insert(dev);
		return m;
	});
}

static
auto on_native_window_resize_impl(sbox::app* app, const sbox::device& dev, ezwin::size window_size) -> void {
	switch (dev.type) {
		case plugin_type::clap: { clap::main::on_native_window_resize(app, dev, window_size); break; }
		case plugin_type::vst3: { /* Not implemented yet. */ break; }
		default:                { assert (false); break; }
	}
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
		ezwin::destroy(device.ui.window);
	}
	const auto result = create_gui(app, device);
	if (!result.success) {
		log(app, "Failed to create GUI for device %d", device.id.value);
		return;
	}
	ezwin::window_config cfg;
	cfg.on_closed = [app, dev_id]{
		const auto& device = app->model.read(ez::main).devices.at(dev_id);
		gui::hide(app, device);
	};
	cfg.on_resized = [app, dev_id](ezwin::size size) {
		const auto& device = app->model.read(ez::main).devices.at(dev_id);
		on_native_window_resize_impl(app, device, size);
	};
	// TOODOO:
	// cfg.parent      = {app->parent_window};
	// cfg.icon        = {};
	cfg.resizable   = {result.resizable};
	cfg.size.width  = static_cast<int>(result.width);
	cfg.size.height = static_cast<int>(result.height);
	cfg.title       = {device.name->c_str()};
	cfg.visible     = {true};
	const auto wnd = ezwin::create(cfg);
	if (!wnd) {
		log(app, "Failed to create window for device %d", device.id.value);
		return;
	}
	device.ui.window = wnd;
	app->msg_sender.enqueue(scuff::msg::out::device_editor_visible_changed{device.id.value, false, (int64_t)(ezwin::get_native_handle(*wnd).value)});
	if (!setup_editor_window(app, device)) {
		log(app, "Failed to setup clap editor window");
	}
	app->model.update(ez::main, [device](model&& m){
		m.devices = m.devices.insert(device);
		return m;
	});
}

} // scuff::sbox::gui
