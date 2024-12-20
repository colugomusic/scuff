#pragma once

#include "clap.hpp"
#include "data.hpp"
#include "os.hpp"

namespace scuff::sbox::gui {

[[nodiscard]] static
auto create_gui(ez::main_t, sbox::app* app, const sbox::device& dev) -> sbox::create_gui_result {
	switch (dev.type) {
		case plugin_type::clap: { return clap::create_gui(ez::main, app, dev); }
		case plugin_type::vst3: { /* Not implemented yet. */ return {}; }
		default:                { assert (false); return {}; }
	}
}

[[nodiscard]] static
auto setup_editor_window(ez::main_t, sbox::app* app, const sbox::device& dev) -> bool {
	switch (dev.type) {
		case plugin_type::clap: { return clap::setup_editor_window(ez::main, app, dev); }
		case plugin_type::vst3: { /* Not implemented yet. */ return false; }
		default:                { assert (false); return false; }
	}
}

static
auto hide(ez::main_t, sbox::app* app, sbox::device dev) -> void {
	if (!dev.ui.window) {
		return;
	}
	switch (dev.type) {
		case plugin_type::clap: { clap::shutdown_editor_window(ez::main, app, dev); break; }
		case plugin_type::vst3: { /* Not implemented yet. */ break; }
		default: break;
	}
	edwin::set(dev.ui.window, edwin::hide);
	app->msgs_out.lock()->push_back(scuff::msg::out::device_editor_visible_changed{dev.id.value, false, (int64_t)(edwin::get_native_handle(*dev.ui.window).value)});
	app->model.update(ez::main, [dev](model&& m){
		m.devices = m.devices.insert(dev);
		return m;
	});
}

static
auto on_native_window_resize_impl(sbox::app* app, const sbox::device& dev, edwin::size window_size) -> void {
	switch (dev.type) {
		case plugin_type::clap: { clap::on_native_window_resize(ez::main, app, dev, window_size); break; }
		case plugin_type::vst3: { /* Not implemented yet. */ break; }
		default:                { assert (false); break; }
	}
}

static
auto show(ez::main_t, sbox::app* app, scuff::id::device dev_id, edwin::fn::on_window_closed on_closed) -> void {
	const auto devices   = app->model.read(ez::main).devices;
	auto device          = devices.at({dev_id});
	const auto has_gui   = device.flags.value & device_flags::has_gui;
	if (!has_gui) {
		LOG_S(WARNING) << "Device " << dev_id.value << " does not have a GUI";
		return;
	}
	if (device.ui.window) {
		edwin::destroy(device.ui.window);
	}
	const auto result = create_gui(ez::main, app, device);
	if (!result.success) {
		LOG_S(ERROR) << "Failed to create GUI for device " << dev_id.value;
		return;
	}
	edwin::window_config cfg;
	cfg.on_closed.fn = [app, dev_id, on_closed]{
		const auto& device = app->model.read(ez::main).devices.at(dev_id);
		gui::hide(ez::main, app, device);
		on_closed.fn();
	};
	cfg.on_resized.fn = [app, dev_id](edwin::size size) {
		const auto& device = app->model.read(ez::main).devices.at(dev_id);
		on_native_window_resize_impl(app, device, size);
	};
	cfg.on_resizing.fn = [app, dev_id](edwin::size size) {
		const auto& device = app->model.read(ez::main).devices.at(dev_id);
		on_native_window_resize_impl(app, device, size);
	};
	cfg.parent    = app->options.parent_window;
	cfg.icon.size   = app->window_icon.size;
	cfg.icon.pixels = app->window_icon.pixels;
	cfg.resizable   = {result.resizable};
	cfg.size.width  = static_cast<int>(result.width);
	cfg.size.height = static_cast<int>(result.height);
	cfg.title       = {device.name->c_str()};
	cfg.visible     = {true};
	const auto wnd = edwin::create(cfg);
	if (!wnd) {
		LOG_S(ERROR) << "Failed to create window for device " << dev_id.value;
		return;
	}
	device.ui.window = wnd;
	app->msgs_out.lock()->push_back(scuff::msg::out::device_editor_visible_changed{device.id.value, false, (int64_t)(edwin::get_native_handle(*wnd).value)});
	if (!setup_editor_window(ez::main, app, device)) {
		LOG_S(ERROR) << "Failed to setup clap editor window";
	}
	app->model.update(ez::main, [device](model&& m){
		m.devices = m.devices.insert(device);
		return m;
	});
}

} // scuff::sbox::gui
