#pragma once

#include "data.hpp"
#include "ui-types.hpp"

namespace scuff {

namespace ui {

static
auto update_editor_window_handle(ez::ui_t, id::device dev_id, int64_t native_handle) -> void {
	DATA_->model.update(ez::ui, [dev_id, native_handle](model&& m) {
		auto dev = m.devices.at(dev_id);
		dev.editor_window_native_handle = (void*)(native_handle);
		m.devices = m.devices.insert(dev);
		return m;
	});
}

template <typename Task> [[nodiscard]] static
auto pop_task(ui::q<Task>* ui_q) -> Task {
	const auto q = ui_q->lock();
	if (q->empty()) {
		return nullptr;
	}
	auto task = q->front();
	q->pop_front();
	return task;
}

static
auto process(ez::ui_t, const general_ui& ui) -> void {
	while (const auto task = pop_task(&DATA_->ui)) {
		task(ui);
	}
}

static
auto process(ez::ui_t, scuff::id::group group_id, const group_ui& ui) -> void {
	const auto m      = DATA_->model.read(ez::ui);
	const auto& group = m.groups.at(group_id);
	while (const auto task = pop_task(&group.service->ui)) {
		task(ui);
	}
}

template <typename Task> static
auto enqueue(ez::nort_t, Task&& task) -> void {
	DATA_->ui.lock()->push_back(std::forward<Task>(task));
}

template <typename Task> static
auto enqueue(ez::nort_t, const scuff::group& group, Task&& task) -> void {
	group.service->ui.lock()->push_back(std::forward<Task>(task));
}

template <typename Task> static
auto enqueue(ez::nort_t, const scuff::sandbox& sbox, Task&& task) -> void {
	if (const auto group = DATA_->model.read(ez::nort).groups.find(sbox.group)) {
		enqueue(ez::nort, *group, std::forward<Task>(task));
	}
}

template <typename UI, typename Fn, typename... Args> static
auto invoke_if_not_null(const UI& ui, Fn fn, Args&&... args) -> void {
	if (ui.*fn) {
		(ui.*fn)(std::forward<Args>(args)...);
	}
}

static
auto error(ez::nort_t, std::string_view error) -> void {
	enqueue(ez::nort, [error](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_error, error);
	});
}

static
auto on_device_editor_visible_changed(ez::nort_t, const sandbox& sbox, id::device dev_id, int64_t native_handle) -> void {
	enqueue(ez::nort, sbox, [dev_id, native_handle](const group_ui& ui) {
		update_editor_window_handle(ez::ui, dev_id, native_handle);
		invoke_if_not_null(ui, &group_ui::on_device_editor_visible_changed, dev_id, true, native_handle);
	});
}

static
auto on_device_flags_changed(ez::nort_t, const sandbox& sbox, id::device dev_id) -> void {
	enqueue(ez::nort, sbox, [dev_id](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_device_flags_changed, dev_id);
	});
}

static
auto on_device_late_create(ez::nort_t, const scuff::group& group, create_device_result result) -> void {
	enqueue(ez::nort, group, [result](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_device_late_create, result);
	});
}

static
auto on_device_late_create(ez::nort_t, const scuff::sandbox& sbox, create_device_result result) -> void {
	enqueue(ez::nort, sbox, [result](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_device_late_create, result);
	});
}

static
auto on_device_params_changed(ez::nort_t, const sandbox& sbox, id::device dev_id) -> void {
	enqueue(ez::nort, sbox, [dev_id](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_device_params_changed, dev_id);
	});
}

static
auto on_device_ports_changed(ez::nort_t, const sandbox& sbox, id::device dev_id) -> void {
	enqueue(ez::nort, sbox, [dev_id](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_device_ports_changed, dev_id);
	});
}

static
auto on_plugfile_broken(ez::nort_t, id::plugfile plugfile_id) -> void {
	enqueue(ez::nort, [plugfile_id](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_plugfile_broken, plugfile_id);
	});
}

static
auto on_plugfile_scanned(ez::nort_t, id::plugfile plugfile_id) -> void {
	enqueue(ez::nort, [plugfile_id](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_plugfile_scanned, plugfile_id);
	});
}

static
auto on_plugin_broken(ez::nort_t, id::plugin plugin_id) -> void {
	enqueue(ez::nort, [plugin_id](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_plugin_broken, plugin_id);
	});
}

static
auto on_plugin_scanned(ez::nort_t, id::plugin plugin_id) -> void {
	enqueue(ez::nort, [plugin_id](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_plugin_scanned, plugin_id);
	});
}

static
auto on_sbox_crashed(ez::nort_t, const sandbox& sbox, std::string_view error) -> void {
	enqueue(ez::nort, sbox, [sbox_id = sbox.id, error](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_sbox_crashed, sbox_id, error);
	});
}

static
auto on_sbox_error(ez::nort_t, const sandbox& sbox, std::string_view error) -> void {
	enqueue(ez::nort, sbox, [sbox_id = sbox.id, error](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_sbox_error, sbox_id, error);
	});
}

static
auto on_sbox_info(ez::nort_t, const sandbox& sbox, std::string_view info) -> void {
	enqueue(ez::nort, sbox, [sbox_id = sbox.id, info](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_sbox_info, sbox_id, info);
	});
}

static
auto on_sbox_warning(ez::nort_t, const sandbox& sbox, std::string_view warning) -> void {
	enqueue(ez::nort, sbox, [sbox_id = sbox.id, warning](const group_ui& ui) {
		invoke_if_not_null(ui, &group_ui::on_sbox_warning, sbox_id, warning);
	});
}

static
auto scan_complete(ez::nort_t) -> void {
	enqueue(ez::nort, [](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_scan_complete);
	});
}

static
auto scan_error(ez::nort_t, std::string_view msg) -> void {
	enqueue(ez::nort, [msg](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_scan_error, msg);
	});
}

static
auto scan_started(ez::nort_t) -> void {
	enqueue(ez::nort, [](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_scan_started);
	});
}

static
auto scan_warning(ez::nort_t, std::string_view msg) -> void {
	enqueue(ez::nort, [msg](const general_ui& ui) {
		invoke_if_not_null(ui, &general_ui::on_scan_warning, msg);
	});
}

} // ui

} // scuff