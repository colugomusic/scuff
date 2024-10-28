#pragma once

#include "data.hpp"
#include "ui-types.hpp"

namespace scuff {
namespace ui {

static
auto send(const ui::msg::general& msg) -> void {
	DATA_->ui.lock()->push_back(msg);
}

static
auto send(const scuff::group& group, const ui::msg::group& msg) -> void {
	group.services->ui.lock()->push_back(msg);
}

static
auto send(const scuff::sandbox& sbox, const ui::msg::group& msg) -> void {
	if (const auto group = DATA_->model.read().groups.find(sbox.group)) {
		send(*group, msg);
	}
}

template <typename T> [[nodiscard]] static
auto pop_msg(ui::msg::q<T>* ui_q) -> std::optional<T> {
	const auto q = ui_q->lock();
	if (q->empty()) {
		return std::nullopt;
	}
	const auto msg = q->front();
	q->pop_front();
	return msg;
}

static auto cb_(const ui::msg::error& msg, const general_ui& ui) -> void                       { ui.on_error(msg.error); } 
static auto cb_(const ui::msg::plugfile_broken& msg, const general_ui& ui) -> void             { ui.on_plugfile_broken(msg.plugfile); }
static auto cb_(const ui::msg::plugfile_scanned& msg, const general_ui& ui) -> void            { ui.on_plugfile_scanned(msg.plugfile); }
static auto cb_(const ui::msg::plugin_broken& msg, const general_ui& ui) -> void               { ui.on_plugin_broken(msg.plugin); }
static auto cb_(const ui::msg::plugin_scanned& msg, const general_ui& ui) -> void              { ui.on_plugin_scanned(msg.plugin); }
static auto cb_(const ui::msg::scan_complete& msg, const general_ui& ui) -> void               { ui.on_scan_complete(); }
static auto cb_(const ui::msg::scan_error& msg, const general_ui& ui) -> void                  { ui.on_scan_error(msg.error); }
static auto cb_(const ui::msg::scan_started& msg, const general_ui& ui) -> void                { ui.on_scan_started(); }
static auto cb_(const ui::msg::scan_warning& msg, const general_ui& ui) -> void                { ui.on_scan_warning(msg.warning); }
static auto cb_(const ui::msg::device_load& msg, const group_ui& ui) -> void                   { ui.on_device_load(msg.result); }
static auto cb_(const ui::msg::device_params_changed& msg, const group_ui& ui) -> void         { ui.on_device_params_changed(msg.dev); }
static auto cb_(const ui::msg::error& msg, const group_ui& ui) -> void                         { ui.on_error(msg.error); }
static auto cb_(const ui::msg::sbox_crashed& msg, const group_ui& ui) -> void                  { ui.on_sbox_crashed(msg.sbox, msg.error); }
static auto cb_(const ui::msg::sbox_error& msg, const group_ui& ui) -> void                    { ui.on_sbox_error(msg.sbox, msg.error); }
static auto cb_(const ui::msg::sbox_info& msg, const group_ui& ui) -> void                     { ui.on_sbox_info(msg.sbox, msg.info); }
static auto cb_(const ui::msg::sbox_started& msg, const group_ui& ui) -> void                  { ui.on_sbox_started(msg.sbox); }
static auto cb_(const ui::msg::sbox_warning& msg, const group_ui& ui) -> void                  { ui.on_sbox_warning(msg.sbox, msg.warning); }
static auto cb_(const ui::msg::device_create& msg, const group_ui& ui) -> void                 { msg.callback(msg.result); }
static auto cb_(const ui::msg::device_state& msg, const group_ui& ui) -> void                  { msg.callback(msg.state); }
static auto cb_(const ui::msg::param_value& msg, const group_ui& ui) -> void                   { msg.callback(msg.value); }
static auto cb_(const ui::msg::param_value_text& msg, const group_ui& ui) -> void              { msg.callback(msg.text); }

static auto cb_(const ui::msg::device_editor_visible_changed& msg, const group_ui& ui) -> void {
	DATA_->model.update([dev_id = msg.dev, native_handle = msg.native_handle](model&& m) {
		auto dev = m.devices.at(dev_id);
		dev.editor_window_native_handle = (void*)(native_handle);
		m.devices = m.devices.insert(dev);
		return m;
	});
	ui.on_device_editor_visible_changed(msg.dev, msg.visible, msg.native_handle);
}

static auto cb(const ui::msg::general& msg, const general_ui& ui) -> void { fast_visit([&ui](const auto& msg) { cb_(msg, ui); }, msg); } 
static auto cb(const ui::msg::group& msg, const group_ui& ui) -> void { fast_visit([&ui](const auto& msg)     { cb_(msg, ui); }, msg); }

static
auto call_callbacks(const general_ui& ui) -> void {
	while (const auto msg = pop_msg(&DATA_->ui)) {
		cb(*msg, ui);
	}
}

static
auto call_callbacks(scuff::id::group group_id, const group_ui& ui) -> void {
	const auto m      = DATA_->model.read();
	const auto& group = m.groups.at(group_id);
	while (const auto msg = pop_msg(&group.services->ui)) {
		cb(*msg, ui);
	}
}

} // ui
} // scuff