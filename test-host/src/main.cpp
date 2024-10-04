#include "common/os.hpp"
#include <algorithm>
#include <cs_plain_guarded.h>
#include <deque>
#include <nappgui.h>
#include <scuff/client.hpp>
#include <stdlib.h>
#include <string>
#include <variant>
#include <vector>

namespace lg = libguarded;

namespace host {

namespace ui {

static constexpr auto MARGIN       = 10.0f;
static constexpr auto MARGIN_SMALL = 5.0f;

struct table {
	Label* title;
	Layout* layout;
	TableView* view;
};

struct plugfile_table : table {
	uint32_t col_path;
	uint32_t col_status;
};

struct plugin_table : table {
	uint32_t col_vendor;
	uint32_t col_name;
	uint32_t col_status;
	uint32_t col_features;
};

struct path_edit {
	Edit* edit;
	Label* title;
	Layout* layout;
};

struct paths {
	Layout* vbox;
	path_edit path_edit_sbox_exe;
	path_edit path_edit_scan_exe;
};

struct log {
	Layout* vbox;
	Label* title;
	TextView* view;
};

struct main {
	Layout* vbox;
	Layout* table_area;
	Layout* top_area;
	Panel* panel;
	Window* window;
	Button* btn_rescan;
	ui::log log;
	ui::plugfile_table plugfile_table;
	ui::plugin_table plugin_table;
	ui::paths paths;
};

} // ui

struct plugfile {
	std::string path;
	std::string status;
	[[nodiscard]] auto operator <=>(const plugfile&) const = default;
};

struct plugin {
	std::string vendor;
	std::string name;
	std::string status;
	std::string features;
	[[nodiscard]] auto operator <=>(const plugin&) const = default;
};

template <typename T>
struct with_dirt : T {
	bool dirty = false;
};

struct app {
	ui::main ui;
	with_dirt<std::vector<plugfile>> plugfiles;
	with_dirt<std::vector<plugin>> plugins;
	//lg::plain_guarded<std::deque<to_main::msg>> to_main;
};

static
auto on_window_close(host::app* app, Event* e) -> void {
	const auto p     = event_params(e, EvWinClose);
	const auto close = event_result(e, bool_t);
	cassert_no_null(app);
	switch (p->origin) {
		case ekGUI_CLOSE_BUTTON: {
			osapp_finish();
			break;
		}
		case ekGUI_CLOSE_DEACT: {
			cassert_default();
		}
	}
}

static
auto on_btn_rescan_clicked(host::app* app, Event* e) -> void {
	scuff::scan(edit_get_text(app->ui.paths.path_edit_scan_exe.edit), {});
}

static
auto on_table_plugfiles_data(host::app* app, Event *e) -> void {
	switch (event_type(e)) {
		case ekGUI_EVENT_TBL_NROWS: {
			const auto result = event_result(e, uint32_t);
			*result = uint32_t(app->plugfiles.size());
			break;
		}
		case ekGUI_EVENT_TBL_CELL: {
			const auto pos  = event_params(e, EvTbPos);
			const auto cell = event_result(e, EvTbCell);
			if (pos->row >= app->plugfiles.size()) { break; }
			if (pos->col == app->ui.plugfile_table.col_path)     { cell->text = app->plugfiles[pos->row].path.c_str(); break; }
			if (pos->col == app->ui.plugfile_table.col_status)   { cell->text = app->plugfiles[pos->row].status.c_str(); break; }
			break;
		}
	}
}

static
auto on_table_plugins_data(host::app* app, Event *e) -> void {
	switch (event_type(e)) {
		case ekGUI_EVENT_TBL_NROWS: {
			const auto result = event_result(e, uint32_t);
			*result = uint32_t(app->plugins.size());
			break;
		}
		case ekGUI_EVENT_TBL_CELL: {
			const auto pos  = event_params(e, EvTbPos);
			const auto cell = event_result(e, EvTbCell);
			if (pos->row >= app->plugins.size()) { break; }
			if (pos->col == app->ui.plugin_table.col_vendor)   { cell->text = app->plugins[pos->row].vendor.c_str(); break; }
			if (pos->col == app->ui.plugin_table.col_name)     { cell->text = app->plugins[pos->row].name.c_str(); break; }
			if (pos->col == app->ui.plugin_table.col_status)   { cell->text = app->plugins[pos->row].status.c_str(); break; }
			if (pos->col == app->ui.plugin_table.col_features) { cell->text = app->plugins[pos->row].features.c_str(); break; }
			break;
		}
	}
}

static
auto create_table(ui::table* table, const char* title) -> void {
	table->title  = label_create();
	table->layout = layout_create(1, 2);
	table->view   = tableview_create();
	tableview_header_resizable(table->view, true);
	label_text(table->title, title);
	layout_label(table->layout, table->title, 0, 0);
	layout_tableview(table->layout, table->view, 0, 1);
	layout_vexpand(table->layout, 1);
	layout_vmargin(table->layout, 0, ui::MARGIN_SMALL);
}

static
auto create_log(ui::log* log) -> void {
	log->title = label_create();
	log->view  = textview_create();
	log->vbox  = layout_create(1, 2);
	label_text(log->title, "Log");
	layout_label(log->vbox, log->title, 0, 0);
	layout_textview(log->vbox, log->view, 0, 1);
	layout_vmargin(log->vbox, 0, ui::MARGIN_SMALL);
	layout_vsize(log->vbox, 1, 300.0f);
}

static
auto create_path_edit(ui::path_edit* pe, const char* title) -> void {
	pe->edit = edit_create();
	pe->layout = layout_create(1, 2);
	pe->title = label_create();
	label_text(pe->title, title);
	layout_label(pe->layout, pe->title, 0, 0);
	layout_edit(pe->layout, pe->edit, 0, 1);
	layout_vmargin(pe->layout, 0, ui::MARGIN_SMALL);
}

static
auto setup_tables(host::app* app) -> void {
	app->ui.plugfile_table.col_path   = tableview_new_column_text(app->ui.plugfile_table.view);
	app->ui.plugfile_table.col_status = tableview_new_column_text(app->ui.plugfile_table.view);
	app->ui.plugin_table.col_vendor   = tableview_new_column_text(app->ui.plugin_table.view);
	app->ui.plugin_table.col_name     = tableview_new_column_text(app->ui.plugin_table.view);
	app->ui.plugin_table.col_status   = tableview_new_column_text(app->ui.plugin_table.view);
	app->ui.plugin_table.col_features = tableview_new_column_text(app->ui.plugin_table.view);
	tableview_header_title(app->ui.plugfile_table.view, app->ui.plugfile_table.col_path, "Path");
	tableview_header_title(app->ui.plugfile_table.view, app->ui.plugfile_table.col_status, "Status");
	tableview_header_title(app->ui.plugin_table.view, app->ui.plugin_table.col_vendor, "Vendor");
	tableview_header_title(app->ui.plugin_table.view, app->ui.plugin_table.col_name, "Name");
	tableview_header_title(app->ui.plugin_table.view, app->ui.plugin_table.col_status, "Status");
	tableview_header_title(app->ui.plugin_table.view, app->ui.plugin_table.col_features, "Features");
	tableview_column_resizable(app->ui.plugfile_table.view, app->ui.plugfile_table.col_path, true);
	tableview_column_resizable(app->ui.plugin_table.view, app->ui.plugin_table.col_vendor, true);
	tableview_column_width(app->ui.plugfile_table.view, app->ui.plugfile_table.col_path, 400);
	tableview_column_width(app->ui.plugin_table.view, app->ui.plugin_table.col_name, 300);
	tableview_OnData(app->ui.plugfile_table.view, listener(app, on_table_plugfiles_data, host::app));
	tableview_OnData(app->ui.plugin_table.view, listener(app, on_table_plugins_data, host::app));
}

static
auto setup_path_editors(host::app* app) -> void {
	create_path_edit(&app->ui.paths.path_edit_sbox_exe, "Sandbox exe path");
	create_path_edit(&app->ui.paths.path_edit_scan_exe, "Scanner exe path");
	layout_vmargin(app->ui.paths.vbox, 0, ui::MARGIN_SMALL);
	edit_text(app->ui.paths.path_edit_sbox_exe.edit, "Z:/dv/_bld/scuff/Debug/bin/scuff-sbox.exe");
	edit_text(app->ui.paths.path_edit_scan_exe.edit, "Z:/dv/_bld/scuff/scan/Debug/scuff-scan.exe");
}

static
auto setup_layouts(host::app* app) -> void {
	panel_layout(app->ui.panel, app->ui.vbox);
	layout_layout(app->ui.vbox, app->ui.top_area, 0, 0);
	layout_layout(app->ui.vbox, app->ui.table_area, 0, 1);
	layout_layout(app->ui.vbox, app->ui.log.vbox, 0, 2);
	layout_layout(app->ui.top_area, app->ui.paths.vbox, 0, 0);
	layout_button(app->ui.top_area, app->ui.btn_rescan, 1, 0);
	layout_halign(app->ui.vbox, 0, 0, ekJUSTIFY);
	layout_vexpand(app->ui.vbox, 1);
	layout_layout(app->ui.table_area, app->ui.plugfile_table.layout, 0, 0);
	layout_layout(app->ui.table_area, app->ui.plugin_table.layout, 1, 0);
	layout_margin(app->ui.vbox, ui::MARGIN);
	layout_vmargin(app->ui.vbox, 0, ui::MARGIN);
	layout_vmargin(app->ui.vbox, 1, ui::MARGIN);
	layout_hmargin(app->ui.table_area, 0, ui::MARGIN);
	layout_layout(app->ui.paths.vbox, app->ui.paths.path_edit_sbox_exe.layout, 0, 0);
	layout_layout(app->ui.paths.vbox, app->ui.paths.path_edit_scan_exe.layout, 0, 1);
	layout_halign(app->ui.top_area, 0, 0, ekJUSTIFY);
	layout_halign(app->ui.top_area, 1, 0, ekRIGHT);
	layout_valign(app->ui.top_area, 1, 0, ekTOP);
	layout_hmargin(app->ui.top_area, 0, ui::MARGIN);
	layout_hexpand(app->ui.top_area, 0);
}

static
auto setup_window(host::app* app) -> void {
	window_title(app->ui.window, "scuff-test-host");
	window_panel(app->ui.window, app->ui.panel);
	window_origin(app->ui.window, v2df(500, 200));
	window_size(app->ui.window, s2df(1400, 1000));
	window_OnClose(app->ui.window, listener(app, on_window_close, host::app));
	window_show(app->ui.window);
}

static
auto setup_buttons(host::app* app) -> void {
	button_text(app->ui.btn_rescan, "Scan system for installed plugins");
	button_OnClick(app->ui.btn_rescan, listener(app, on_btn_rescan_clicked, host::app));
}

static
auto create_window(host::app* app) -> void {
	app->ui.window      = window_create(ekWINDOW_STDRES);
	app->ui.panel       = panel_create();
	app->ui.vbox        = layout_create(1, 3);
	app->ui.top_area    = layout_create(2, 1);
	app->ui.paths.vbox  = layout_create(1, 2);
	app->ui.table_area  = layout_create(2, 1);
	app->ui.btn_rescan  = button_push();
	create_log(&app->ui.log);
	create_table(&app->ui.plugfile_table, "Plugin Files");
	create_table(&app->ui.plugin_table, "Plugins");
	setup_buttons(app);
	setup_tables(app);
	setup_path_editors(app);
	setup_layouts(app);
	setup_window(app);
}

static
auto on_scuff_error(host::app* app, std::string_view error) -> void {
	textview_printf(app->ui.log.view, "%s\n", error.data());
}

static
auto on_scuff_plugfile_broken(host::app* app, scuff::id::plugfile pf) -> void {
	plugfile my_pf;
	my_pf.path   = scuff::get_path(pf);
	my_pf.status = scuff::get_error(pf);
	app->plugfiles.push_back(my_pf);
	app->plugfiles.dirty = true;
}

static
auto on_scuff_plugfile_scanned(host::app* app, scuff::id::plugfile pf) -> void {
	plugfile my_pf;
	my_pf.path   = scuff::get_path(pf);
	my_pf.status = "Working";
	app->plugfiles.push_back(my_pf);
	app->plugfiles.dirty = true;
}

[[nodiscard]] static
auto as_comma_separated_list_string(const std::vector<std::string>& vec) -> std::string {
	std::string out;
	for (const auto& s : vec) {
		out += s;
		out += ", ";
	}
	if (!out.empty()) {
		out.pop_back();
		out.pop_back();
	}
	return out;
}

static
auto on_scuff_plugin_broken(host::app* app, scuff::id::plugin p) -> void {
	host::plugin plugin;
	plugin.name     = scuff::get_name(p);
	plugin.vendor   = scuff::get_vendor(p);
	plugin.status   = scuff::get_error(p);
	plugin.features = as_comma_separated_list_string(scuff::get_features(p));
	app->plugins.push_back(plugin);
	app->plugins.dirty = true;
}

static
auto on_scuff_plugin_scanned(host::app* app, scuff::id::plugin p) -> void {
	host::plugin plugin;
	plugin.name     = scuff::get_name(p);
	plugin.vendor   = scuff::get_vendor(p);
	plugin.features = as_comma_separated_list_string(scuff::get_features(p));
	plugin.status   = "Working";
	app->plugins.push_back(plugin);
	app->plugins.dirty = true;
}

static
auto on_scuff_sbox_crashed(host::app* app, scuff::id::sandbox sbox, const char* error) -> void {
	textview_writef(app->ui.log.view, "Sandbox crashed\n");
}

static
auto on_scuff_sbox_started(host::app* app, scuff::id::sandbox sbox) -> void {
	textview_writef(app->ui.log.view, "Sandbox started\n");
}

static
auto on_scuff_scan_complete(host::app* app) -> void {
	textview_writef(app->ui.log.view, "Scan complete\n");
}

static
auto on_scuff_scan_error(host::app* app, std::string_view error) -> void {
	textview_printf(app->ui.log.view, "%s\n", error.data());
}

static
auto on_scuff_scan_started(host::app* app) -> void {
	textview_writef(app->ui.log.view, "Scan started\n");
	app->plugfiles.clear();
	app->plugfiles.dirty = true;
	app->plugins.clear();
	app->plugins.dirty = true;
}

static
auto initialize_scuff(host::app* app) -> void {
	auto on_error = [app](auto... args) { on_scuff_error(app, args...); };
	scuff::init(on_error);
}

[[nodiscard]] static
auto create() -> host::app* {
	const auto app = new host::app;
	create_window(app);
	initialize_scuff(app);
	return app;
}

static
auto destroy(host::app** app) -> void {
	scuff::shutdown();
	window_destroy(&(*app)->ui.window);
	delete *app;
	*app = nullptr;
}

template <typename T, size_t N> [[nodiscard]] static
auto take_some(std::deque<T>* vec) -> std::vector<T> {
	std::vector<T> out;
	out.reserve(std::min(N, vec->size()));
	for (size_t i = 0; i < N && !vec->empty(); ++i) {
		out.push_back(std::move(vec->front()));
		vec->pop_front();
	}
	return out;
}

static
auto update(host::app* app, double prtime, double ctime) -> void {
	scuff::general_reporter reporter;
	reporter.on_error            = [app](auto... args) { on_scuff_error(app, args...); };
	reporter.on_plugfile_broken  = [app](auto... args) { on_scuff_plugfile_broken(app, args...); };
	reporter.on_plugfile_scanned = [app](auto... args) { on_scuff_plugfile_scanned(app, args...); };
	reporter.on_plugin_broken    = [app](auto... args) { on_scuff_plugin_broken(app, args...); };
	reporter.on_plugin_scanned   = [app](auto... args) { on_scuff_plugin_scanned(app, args...); };
	reporter.on_scan_complete    = [app](auto... args) { on_scuff_scan_complete(app); };
	reporter.on_scan_error       = [app](auto... args) { on_scuff_scan_error(app, args...); };
	reporter.on_scan_started     = [app](auto... args) { on_scuff_scan_started(app); };
	scuff::receive_report(reporter);
	if (app->plugfiles.dirty) {
		std::sort(app->plugfiles.begin(), app->plugfiles.end());
		tableview_update(app->ui.plugfile_table.view);
		app->plugfiles.dirty = false;
	}
	if (app->plugins.dirty) {
		std::sort(app->plugins.begin(), app->plugins.end());
		tableview_update(app->ui.plugin_table.view);
		app->plugins.dirty = false;
	}
}

} // host

#include <osmain.h>
osmain_sync(0.1f, host::create, host::destroy, host::update, "", host::app)
