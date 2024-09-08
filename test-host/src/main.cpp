#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <nappgui.h>
#include <scuff/client.h>

namespace host {

struct app {
	Layout* layout_main;
	//Layout* layout_tables;
	//Layout* layout_plugfile_table;
	//Layout* layout_plugin_table;
	//Label* lbl_plugfile_table_title;
	//Label* lbl_plugin_table_title;
	//Panel* panel;
	Window* window;
	//Button* btn_rescan;
	//TableView* table_plugfiles;
	//TableView* table_plugins;
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
auto create_window(host::app* app) -> void {
	static constexpr auto MARGIN       = 10.0f;
	static constexpr auto MARGIN_SMALL = 5.0f;
	app->window                   = window_create(ekWINDOW_STDRES);
	//app->panel                    = panel_create();
	//app->layout_main              = layout_create(1, 2);
	//app->layout_tables            = layout_create(2, 1);
	//app->layout_plugfile_table    = layout_create(1, 2);
	//app->layout_plugin_table      = layout_create(1, 2);
	//app->btn_rescan               = button_push();
	//app->table_plugfiles          = tableview_create();
	//app->table_plugins            = tableview_create();
	//app->lbl_plugfile_table_title = label_create();
	//app->lbl_plugin_table_title   = label_create();
	//label_text(app->lbl_plugfile_table_title, "Plugin Files");
	//label_text(app->lbl_plugin_table_title, "Plugins");
	//panel_layout(app->panel, app->layout_main);
	//layout_vexpand(app->layout_plugfile_table, 1);
	//layout_vexpand(app->layout_plugin_table, 1);
	//layout_label(app->layout_plugfile_table, app->lbl_plugfile_table_title, 0, 0);
	//layout_tableview(app->layout_plugfile_table, app->table_plugfiles, 0, 1);
	//layout_label(app->layout_plugin_table, app->lbl_plugin_table_title, 0, 0);
	//layout_tableview(app->layout_plugin_table, app->table_plugins, 0, 1);
	//layout_button(app->layout_main, app->btn_rescan, 0, 0);
	//layout_halign(app->layout_main, 0, 0, ekCENTER);
	//layout_vexpand(app->layout_main, 1);
	//layout_layout(app->layout_main, app->layout_tables, 0, 1);
	//layout_layout(app->layout_tables, app->layout_plugfile_table, 0, 0);
	//layout_layout(app->layout_tables, app->layout_plugin_table, 1, 0);
	//layout_margin(app->layout_main, MARGIN);
	//layout_vmargin(app->layout_main, 0, MARGIN);
	//layout_vmargin(app->layout_plugfile_table, 0, MARGIN_SMALL);
	//layout_vmargin(app->layout_plugin_table, 0, MARGIN_SMALL);
	//layout_hmargin(app->layout_tables, 0, MARGIN);
	//button_text(app->btn_rescan, "Scan system for installed plugins");
	window_title(app->window, "scuff-test-host");
	//window_panel(app->window, app->panel);
	//window_origin(app->window, v2df(500, 200));
	//window_size(app->window, s2df(800, 600));
	window_OnClose(app->window, listener(app, on_window_close, host::app));
	window_show(app->window);
}

//static
//auto on_scuff_plugfile_broken(const scuff_on_plugfile_broken* ctx, scuff_plugfile pf) -> void {
//	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
//	// TODO:
//}
//
//static
//auto on_scuff_plugfile_scanned(const scuff_on_plugfile_scanned* ctx, scuff_plugfile pf) -> void {
//	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
//	// TODO:
//}
//
//static
//auto on_scuff_plugin_broken(const scuff_on_plugin_broken* ctx, scuff_plugin p) -> void {
//	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
//	// TODO:
//}
//
//static
//auto on_scuff_plugin_scanned(const scuff_on_plugin_scanned* ctx, scuff_plugin p) -> void {
//	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
//	// TODO:
//}
//
//static
//auto on_scuff_sbox_crashed(const scuff_on_sbox_crashed* ctx, scuff_sbox sbox) -> void {
//	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
//}
//
//static
//auto on_scuff_sbox_started(const scuff_on_sbox_started* ctx, scuff_sbox sbox) -> void {
//	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
//}
//
//static
//auto on_scuff_scan_complete(const scuff_on_scan_complete* ctx) -> void {
//	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
//}
//
//static
//auto on_scuff_scan_error(const scuff_on_scan_error* ctx, const char* error) -> void {
//	const auto app = reinterpret_cast<host::app*>(ctx->ctx);
//}
//
//template <typename Cb, typename Fn> [[nodiscard]] static
//auto make_scuff_cb(Fn fn, host::app* app) -> Cb {
//	Cb cb;
//	cb.ctx = app;
//	cb.fn = fn;
//	return cb;
//}

static
auto initialize_scuff(host::app* app) -> void {
	//scuff_config cfg;
	//cfg.gc_interval_ms   = 1000;
	//cfg.sandbox_exe_path = "scuff-sbox.exe";
	//cfg.scanner_exe_path = "scuff-scan.exe";
	//cfg.string_options.max_in_flight_strings = 100;
	//cfg.string_options.max_string_length     = 256;
	//cfg.callbacks.on_plugfile_broken         = make_scuff_cb<scuff_on_plugfile_broken>(on_scuff_plugfile_broken, app);
	//cfg.callbacks.on_plugfile_scanned        = make_scuff_cb<scuff_on_plugfile_scanned>(on_scuff_plugfile_scanned, app);
	//cfg.callbacks.on_plugin_broken           = make_scuff_cb<scuff_on_plugin_broken>(on_scuff_plugin_broken, app);
	//cfg.callbacks.on_plugin_scanned          = make_scuff_cb<scuff_on_plugin_scanned>(on_scuff_plugin_scanned, app);
	//cfg.callbacks.on_sbox_crashed            = make_scuff_cb<scuff_on_sbox_crashed>(on_scuff_sbox_crashed, app);
	//cfg.callbacks.on_sbox_started            = make_scuff_cb<scuff_on_sbox_started>(on_scuff_sbox_started, app);
	//cfg.callbacks.on_scan_complete           = make_scuff_cb<scuff_on_scan_complete>(on_scuff_scan_complete, app);
	//cfg.callbacks.on_scan_error              = make_scuff_cb<scuff_on_scan_error>(on_scuff_scan_error, app);
	//scuff_init(&cfg);
}

[[nodiscard]] static
auto create() -> host::app* {
	const auto app = heap_new0(host::app);
	osapp_finish();
	//create_window(app);
	//initialize_scuff(app);
	return app;
}

static
auto destroy(host::app** app) -> void {
	scuff_shutdown();
	if ((*app)->window) {
		window_destroy(&(*app)->window);
	}
	heap_delete(app, host::app);
}

} // host

#include <osmain.h>
osmain(host::create, host::destroy, "", host::app)
