#include <boost/container/static_vector.hpp>
#include <cs_plain_guarded.h>
#include <nappgui.h>
#include <scuff/client.h>
#include <stdlib.h>
#include <string>
#include <variant>
#include <vector>

namespace lg = libguarded;

namespace host {

namespace to_main {

struct plugfile_broken  { scuff_plugfile plugfile; };
struct plugfile_scanned { scuff_plugfile plugfile; };
struct plugin_broken    { scuff_plugin plugin; };
struct plugin_scanned   { scuff_plugin plugin; };

using msg = std::variant<
	plugfile_broken,
	plugfile_scanned,
	plugin_broken,
	plugin_scanned
>;

} // to_main

struct plugfile {
	std::string path;
	std::string status;
};

struct plugin {
	std::string vendor;
	std::string name;
	std::string status;
};

struct plugfile_table {
	uint32_t col_path;
	uint32_t col_status;
	TableView* view;
	std::vector<plugfile> data;
};

struct plugin_table {
	uint32_t col_vendor;
	uint32_t col_name;
	uint32_t col_status;
	TableView* view;
	std::vector<plugin> data;
};

struct app {
	Layout* layout_main;
	Layout* layout_tables;
	Layout* layout_plugfile_table;
	Layout* layout_plugin_table;
	Label* lbl_plugfile_table_title;
	Label* lbl_plugin_table_title;
	Panel* panel;
	Window* window;
	Button* btn_rescan;
	plugfile_table table_plugfiles;
	plugin_table table_plugins;
	lg::plain_guarded<std::vector<to_main::msg>> to_main;
};

static
auto on_window_close(host::app* app, Event* e) -> void {
	const auto p     = event_params(e, EvWinClose);
	const auto close = event_result(e, bool_t);
	cassert_no_null(app);
	switch (p->origin) {
		case ekGUI_CLOSE_BUTTON: {
			// TODO:
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
	scuff_scan();
}

static
auto on_table_plugfiles_data(host::app* app, Event *e) -> void {
	switch (event_type(e)) {
		case ekGUI_EVENT_TBL_NROWS: {
			const auto result = event_result(e, uint32_t);
			*result = uint32_t(app->table_plugfiles.data.size());
			break;
		}
		case ekGUI_EVENT_TBL_CELL: {
			const auto pos  = event_params(e, EvTbPos);
			const auto cell = event_result(e, EvTbCell);
			if (pos->col == app->table_plugfiles.col_path) {
				cell->text = app->table_plugfiles.data[pos->row].path.c_str();
				break;
			}
			if (pos->col == app->table_plugfiles.col_status) {
				cell->text = app->table_plugfiles.data[pos->row].status.c_str();
				break;
			}
			break;
		}
	}
}

static
auto on_table_plugins_data(host::app* app, Event *e) -> void {
	switch (event_type(e)) {
		case ekGUI_EVENT_TBL_NROWS: {
			const auto result = event_result(e, uint32_t);
			*result = uint32_t(app->table_plugins.data.size());
			break;
		}
		case ekGUI_EVENT_TBL_CELL: {
			const auto pos  = event_params(e, EvTbPos);
			const auto cell = event_result(e, EvTbCell);
			if (pos->col == app->table_plugins.col_vendor) {
				cell->text = app->table_plugins.data[pos->row].vendor.c_str();
				break;
			}
			if (pos->col == app->table_plugins.col_name) {
				cell->text = app->table_plugins.data[pos->row].name.c_str();
				break;
			}
			if (pos->col == app->table_plugins.col_status) {
				cell->text = app->table_plugins.data[pos->row].status.c_str();
				break;
			}
			break;
		}
	}
}

static
auto create_window(host::app* app) -> void {
	static constexpr auto MARGIN       = 10.0f;
	static constexpr auto MARGIN_SMALL = 5.0f;
	app->window                     = window_create(ekWINDOW_STDRES);
	app->panel                      = panel_create();
	app->layout_main                = layout_create(1, 2);
	app->layout_tables              = layout_create(2, 1);
	app->layout_plugfile_table      = layout_create(1, 2);
	app->layout_plugin_table        = layout_create(1, 2);
	app->btn_rescan                 = button_push();
	app->lbl_plugfile_table_title   = label_create();
	app->lbl_plugin_table_title     = label_create();
	app->table_plugfiles.view       = tableview_create();
	app->table_plugins.view         = tableview_create();
	app->table_plugfiles.col_path   = tableview_new_column_text(app->table_plugfiles.view);
	app->table_plugfiles.col_status = tableview_new_column_text(app->table_plugfiles.view);
	app->table_plugins.col_vendor   = tableview_new_column_text(app->table_plugins.view);
	app->table_plugins.col_name     = tableview_new_column_text(app->table_plugins.view);
	app->table_plugins.col_status   = tableview_new_column_text(app->table_plugins.view);
	tableview_header_title(app->table_plugfiles.view, app->table_plugfiles.col_path, "Path");
	tableview_header_title(app->table_plugfiles.view, app->table_plugfiles.col_status, "Status");
	tableview_header_title(app->table_plugins.view, app->table_plugins.col_vendor, "Vendor");
	tableview_header_title(app->table_plugins.view, app->table_plugins.col_name, "Name");
	tableview_header_title(app->table_plugins.view, app->table_plugins.col_status, "Status");
	tableview_header_resizable(app->table_plugfiles.view, true);
	tableview_header_resizable(app->table_plugins.view, true);
	tableview_column_resizable(app->table_plugfiles.view, app->table_plugfiles.col_path, true);
	tableview_column_resizable(app->table_plugins.view, app->table_plugins.col_vendor, true);
	tableview_column_width(app->table_plugfiles.view, app->table_plugfiles.col_path, 400);
	tableview_column_width(app->table_plugins.view, app->table_plugins.col_name, 300);
	tableview_OnData(app->table_plugfiles.view, listener(app, on_table_plugfiles_data, host::app));
	tableview_OnData(app->table_plugins.view, listener(app, on_table_plugins_data, host::app));
	label_text(app->lbl_plugfile_table_title, "Plugin Files");
	label_text(app->lbl_plugin_table_title, "Plugins");
	panel_layout(app->panel, app->layout_main);
	layout_vexpand(app->layout_plugfile_table, 1);
	layout_vexpand(app->layout_plugin_table, 1);
	layout_label(app->layout_plugfile_table, app->lbl_plugfile_table_title, 0, 0);
	layout_tableview(app->layout_plugfile_table, app->table_plugfiles.view, 0, 1);
	layout_label(app->layout_plugin_table, app->lbl_plugin_table_title, 0, 0);
	layout_tableview(app->layout_plugin_table, app->table_plugins.view, 0, 1);
	layout_button(app->layout_main, app->btn_rescan, 0, 0);
	layout_halign(app->layout_main, 0, 0, ekCENTER);
	layout_vexpand(app->layout_main, 1);
	layout_layout(app->layout_main, app->layout_tables, 0, 1);
	layout_layout(app->layout_tables, app->layout_plugfile_table, 0, 0);
	layout_layout(app->layout_tables, app->layout_plugin_table, 1, 0);
	layout_margin(app->layout_main, MARGIN);
	layout_vmargin(app->layout_main, 0, MARGIN);
	layout_vmargin(app->layout_plugfile_table, 0, MARGIN_SMALL);
	layout_vmargin(app->layout_plugin_table, 0, MARGIN_SMALL);
	layout_hmargin(app->layout_tables, 0, MARGIN);
	button_text(app->btn_rescan, "Scan system for installed plugins");
	button_OnClick(app->btn_rescan, listener(app, on_btn_rescan_clicked, host::app));
	window_title(app->window, "scuff-test-host");
	window_panel(app->window, app->panel);
	window_origin(app->window, v2df(500, 200));
	window_size(app->window, s2df(1200, 600));
	window_OnClose(app->window, listener(app, on_window_close, host::app));
	window_show(app->window);
}

static
auto on_scuff_plugfile_broken(const scuff_on_plugfile_broken* ctx, scuff_plugfile pf) -> void {
	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
	app->to_main.lock()->push_back(to_main::plugfile_broken{pf});
}

static
auto on_scuff_plugfile_scanned(const scuff_on_plugfile_scanned* ctx, scuff_plugfile pf) -> void {
	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
	app->to_main.lock()->push_back(to_main::plugfile_scanned{pf});
}

static
auto on_scuff_plugin_broken(const scuff_on_plugin_broken* ctx, scuff_plugin p) -> void {
	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
	app->to_main.lock()->push_back(to_main::plugin_broken{p});
}

static
auto on_scuff_plugin_scanned(const scuff_on_plugin_scanned* ctx, scuff_plugin p) -> void {
	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
	app->to_main.lock()->push_back(to_main::plugin_scanned{p});
}

static
auto on_scuff_sbox_crashed(const scuff_on_sbox_crashed* ctx, scuff_sbox sbox) -> void {
	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
}

static
auto on_scuff_sbox_started(const scuff_on_sbox_started* ctx, scuff_sbox sbox) -> void {
	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
}

static
auto on_scuff_scan_complete(const scuff_on_scan_complete* ctx) -> void {
	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
}

static
auto on_scuff_scan_error(const scuff_on_scan_error* ctx, const char* error) -> void {
	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
	log_printf("scuff_scan_error: %s\n", error);
}

template <typename Cb, typename Fn> [[nodiscard]] static
auto make_scuff_cb(Fn fn, host::app* app) -> Cb {
	Cb cb;
	cb.ctx = app;
	cb.fn = fn;
	return cb;
}

static
auto initialize_scuff(host::app* app) -> void {
	scuff_config cfg;
	cfg.gc_interval_ms   = 1000;
	cfg.sandbox_exe_path = "scuff-sbox.exe";
	cfg.scanner_exe_path = "Z://dv//_bld//scuff//scanner//Debug//scuff-scanner.exe";
	cfg.string_options.max_in_flight_strings = 100;
	cfg.string_options.max_string_length     = 256;
	cfg.callbacks.on_plugfile_broken         = make_scuff_cb<scuff_on_plugfile_broken>(on_scuff_plugfile_broken, app);
	cfg.callbacks.on_plugfile_scanned        = make_scuff_cb<scuff_on_plugfile_scanned>(on_scuff_plugfile_scanned, app);
	cfg.callbacks.on_plugin_broken           = make_scuff_cb<scuff_on_plugin_broken>(on_scuff_plugin_broken, app);
	cfg.callbacks.on_plugin_scanned          = make_scuff_cb<scuff_on_plugin_scanned>(on_scuff_plugin_scanned, app);
	cfg.callbacks.on_sbox_crashed            = make_scuff_cb<scuff_on_sbox_crashed>(on_scuff_sbox_crashed, app);
	cfg.callbacks.on_sbox_started            = make_scuff_cb<scuff_on_sbox_started>(on_scuff_sbox_started, app);
	cfg.callbacks.on_scan_complete           = make_scuff_cb<scuff_on_scan_complete>(on_scuff_scan_complete, app);
	cfg.callbacks.on_scan_error              = make_scuff_cb<scuff_on_scan_error>(on_scuff_scan_error, app);
	try {
		scuff_init(&cfg);
	} catch (const std::exception& e) {
		fprintf(stderr, "scuff_init failed: %s\n", e.what());
	}
}

[[nodiscard]] static
auto create() -> host::app* {
	const auto app = new host::app;
	initialize_scuff(app);
	create_window(app);
	return app;
}

static
auto destroy(host::app** app) -> void {
	scuff_shutdown();
	window_destroy(&(*app)->window);
	delete *app;
	*app = nullptr;
}

static
auto get_messages_to_main(host::app* app) -> std::vector<to_main::msg> {
	const auto queue = app->to_main.lock();
	const auto msgs = *queue;
	queue->clear();
	return msgs;
}

static
auto process_(host::app* app, const to_main::plugfile_broken& msg) -> void {
	plugfile my_pf;
	my_pf.path   = scuff_plugfile_get_path(msg.plugfile);
	my_pf.status = scuff_plugfile_get_error(msg.plugfile);
	app->table_plugfiles.data.push_back(my_pf);
	tableview_update(app->table_plugfiles.view);
}

static
auto process_(host::app* app, const to_main::plugfile_scanned& msg) -> void {
	plugfile my_pf;
	my_pf.path   = scuff_plugfile_get_path(msg.plugfile);
	my_pf.status = "Working";
	app->table_plugfiles.data.push_back(my_pf);
	tableview_update(app->table_plugfiles.view);
}

static
auto process_(host::app* app, const to_main::plugin_broken& msg) -> void {
	host::plugin plugin;
	plugin.name = scuff_plugin_get_name(msg.plugin);
	plugin.vendor = scuff_plugin_get_vendor(msg.plugin);
	plugin.status = scuff_plugin_get_error(msg.plugin);
	app->table_plugins.data.push_back(plugin);
	tableview_update(app->table_plugins.view);
}

static
auto process_(host::app* app, const to_main::plugin_scanned& msg) -> void {
	host::plugin plugin;
	plugin.name = scuff_plugin_get_name(msg.plugin);
	plugin.vendor = scuff_plugin_get_vendor(msg.plugin);
	plugin.status = "Working";
	app->table_plugins.data.push_back(plugin);
	tableview_update(app->table_plugins.view);
}

static
auto process(host::app* app, const to_main::msg& msg) -> void {
	std::visit([app](const auto& msg) { process_(app, msg); }, msg);
}

static
auto update(host::app* app, double prtime, double ctime) -> void {
	const auto msgs = get_messages_to_main(app);
	for (const auto& msg : msgs) {
		process(app, msg);
	}
}

} // host

#include <osmain.h>
osmain_sync(0.1f, host::create, host::destroy, host::update, "", host::app)
