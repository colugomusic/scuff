#pragma once

#include "common-os-child-proc.hpp"
#include "common-plugin-type.hpp"
#include "data.hpp"
#include "ui.hpp"
#include <boost/process/v1/async_pipe.hpp>
#include <deque>
#include <format>
#include <nlohmann/json.hpp>
#pragma warning(push, 0)
#include <immer/vector_transient.hpp>
#pragma warning(pop)

namespace bp   = boost::process;
namespace bsys = boost::system;

namespace scuff {
namespace scan_ {

struct scanner {
	basio::io_context context;
	std::string_view exe_path;
	std::deque<basio::streambuf> buffers;
	std::deque<bp::v1::async_pipe> pipes;
	std::deque<bp::v1::child> procs;
	scan_flags flags;
};

struct reader {
	basio::streambuf* buffer;
	bp::v1::async_pipe* pipe;
};

struct scanner_process {
	reader stderr_reader;
	reader stdout_reader;
};

using respond_fn = auto(*)(scan_::scanner* scanner, const nlohmann::json& j) -> void;

static auto read_stderr_lines(scan_::scanner* scanner, const std::vector<std::string>& args, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;
static auto read_stdout_lines(scan_::scanner* scanner, const std::vector<std::string>& args, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;

[[nodiscard]] static
auto add_buffer(scan_::scanner* scanner) -> basio::streambuf* {
	scanner->buffers.emplace_back();
	return &scanner->buffers.back();
}

[[nodiscard]] static
auto add_pipe(scan_::scanner* scanner) -> bp::v1::async_pipe* {
	scanner->pipes.push_back(bp::v1::async_pipe{scanner->context});
	return &scanner->pipes.back();
}

static
auto start_scanner_process(scan_::scanner* scanner, const std::vector<std::string>& args, bp::v1::async_pipe* pipe_out, bp::v1::async_pipe* pipe_err) -> void {
	scanner->procs.emplace_back(os::start_child_process(scanner->exe_path.data(), args, bp::v1::std_out > *pipe_out, bp::v1::std_err > *pipe_err, scanner->context));
}

[[nodiscard]] static
auto start_scanner_process(scan_::scanner* scanner, const std::vector<std::string>& args) -> scanner_process {
	scanner_process proc;
	proc.stderr_reader.buffer = add_buffer(scanner);
	proc.stdout_reader.buffer = add_buffer(scanner);
	proc.stderr_reader.pipe   = add_pipe(scanner);
	proc.stdout_reader.pipe   = add_pipe(scanner);
	start_scanner_process(scanner, args, proc.stdout_reader.pipe, proc.stderr_reader.pipe);
	return proc;
}

[[nodiscard]] static
auto make_exe_args_for_plugin_listing() -> std::vector<std::string> {
	return {};
}

[[nodiscard]] static
auto make_exe_args_for_scanning_plugin(const std::string& path) -> std::vector<std::string> {
	std::vector<std::string> args;
	args.push_back("--file");
	args.push_back(path);
	return args;
}

template <typename ReadFn> static
auto async_read_lines(scan_::scanner* scanner, const std::vector<std::string>& args, scan_::reader reader, ReadFn&& read_fn) -> void {
	auto wrapper_fn = [scanner, args, reader, read_fn = std::forward<ReadFn>(read_fn)](const bsys::error_code& ec, size_t bytes_transferred) {
		read_fn(scanner, args, reader, ec, bytes_transferred);
	};
	basio::async_read_until(*reader.pipe, *reader.buffer, '\n', wrapper_fn);
}

static
auto read_broken_plugfile(const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	const std::string error         = j["error"];
	scuff::plugfile pf;
	pf.id    = id::plugfile{id_gen_++};
	pf.path  = path;
	pf.error = error;
	pf.type  = scuff::plugin_type_from_string(plugfile_type);
	DATA_->model.update(ez::nort, [pf](model&& m){
		m.plugfiles = m.plugfiles.insert(pf);
		return m;
	});
	ui::on_plugfile_broken(ez::nort, pf.id);
}

[[nodiscard]] static
auto find_plugfile_from_path(std::string_view path) -> id::plugfile {
	const auto m         = DATA_->model.read(ez::nort);
	for (const auto& pf : m.plugfiles) {
		if (pf.path == path) {
			return pf.id;
		}
	}
	return id::plugfile{};
}

static
auto read_broken_plugin(const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	const std::string error         = j["error"];
	const auto type = scuff::plugin_type_from_string(plugfile_type);
	if (type == scuff::plugin_type::clap) {
		const std::string name    = j["name"];
		const std::string id      = j["id"];
		const std::string url     = j["url"];
		const std::string vendor  = j["vendor"];
		const std::string version = j["version"];
		const std::string path    = j["path"];
		scuff::plugin plugin;
		plugin.id       = id::plugin{id_gen_++};
		plugin.ext_id   = ext::id::plugin{id};
		plugin.name     = name;
		plugin.type     = type;
		plugin.vendor   = vendor;
		plugin.version  = version;
		plugin.plugfile = find_plugfile_from_path(path);
		DATA_->model.update(ez::nort, [plugin](model&& m){
			m.plugins = m.plugins.insert(plugin);
			return m;
		});
		ui::on_plugin_broken(ez::nort, plugin.id);
	}
}

static
auto async_scan_clap_file(scan_::scanner* scanner, const std::string& path) -> void {
	const auto exe_args = make_exe_args_for_scanning_plugin(path);
	const auto proc     = start_scanner_process(scanner, exe_args);
	async_read_lines(scanner, exe_args, proc.stderr_reader, read_stderr_lines);
	async_read_lines(scanner, exe_args, proc.stdout_reader, read_stdout_lines);
}

static
auto read_plugfile(scan_::scanner* scanner, const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	plugfile pf;
	pf.id   = id::plugfile{id_gen_++};
	pf.path = path;
	pf.type = scuff::plugin_type_from_string(plugfile_type);
	DATA_->model.update(ez::nort, [pf](model&& m){
		m.plugfiles = m.plugfiles.insert(pf);
		return m;
	});
	ui::on_plugfile_scanned(ez::nort, pf.id);
	basio::post(scanner->context, [scanner, path] { async_scan_clap_file(scanner, path); });
}

[[nodiscard]] static
auto to_immer(const std::vector<std::string>& strings) -> immer::vector<std::string> {
	auto t = immer::vector_transient<std::string>{};
	for (const auto& str : strings) {
		t.push_back(str);
	}
	return t.persistent();
}

[[nodiscard]] static
auto find_existing_plugin(const scuff::model& m, std::string_view id) -> std::optional<scuff::plugin> {
	for (const auto& plugin : m.plugins) {
		if (plugin.ext_id.value == id) {
			return plugin;
		}
	}
	return std::nullopt;
}

static
auto retry_failed_devices(scuff::plugin plugin, scuff::scan_flags flags) -> void {
	if (flags.value & scuff::scan_flags::retry_failed_devices) {
		auto m = DATA_->model.read(ez::nort);
		for (auto dev : m.devices) {
			if (!dev.plugin && dev.plugin_ext_id == plugin.ext_id && dev.type == plugin.type) {
				const auto sbox     = m.sandboxes.at(dev.sbox);
				const auto plugfile = m.plugfiles.at(plugin.plugfile);
				const auto fn = [group = m.groups.at(sbox.group), cb = dev.creation_callback](scuff::create_device_result result) {
					if (cb) {
						cb(result);
					}
					ui::on_device_late_create(ez::nort, group, result);
				};
				const auto callback = sbox.service->return_buffers.device_create_results.put(fn);
				dev.plugin            = plugin.id;
				dev.creation_callback = {};
				m.devices = m.devices.insert(dev);
				DATA_->model.set(ez::nort, m);
				sbox.service->enqueue(msg::in::device_create{dev.id.value, dev.type, plugfile.path, plugin.ext_id.value, callback});
			}
		}
	}
}

static
auto read_plugin(scan_::scanner* scanner, const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	const auto type = scuff::plugin_type_from_string(plugfile_type);
	switch (type) {
		case scuff::plugin_type::clap: {
			const std::string name                  = j["name"];
			const std::string id                    = j["id"];
			const std::string url                   = j["url"];
			const std::string vendor                = j["vendor"];
			const std::string version               = j["version"];
			const std::string path                  = j["path"];
			const std::vector<std::string> features = j["features"];
			const bool has_gui                      = j["has-gui"];
			const bool has_params                   = j["has-params"];
			// Check if plugin id is already known.
			// If it is, check if the version is higher.
			// If it is, update the existing plugin entry.
			// If it isn't ignore this plugin.
			auto m = DATA_->model.read(ez::nort);
			if (const auto existing_plugin = find_existing_plugin(m, id)) {
				ui::scan_warning(ez::nort, std::format("The scanner found multiple plugins with the same id: '{}'", id));
				if (version.compare(existing_plugin->version) <= 0) {
					return;
				}
			}
			scuff::plugin plugin;
			plugin.id            = id::plugin{id_gen_++};
			plugin.ext_id        = ext::id::plugin{id};
			plugin.name          = name;
			plugin.type          = type;
			plugin.vendor        = vendor;
			plugin.version       = version;
			plugin.clap_features = to_immer(features);
			plugin.plugfile      = find_plugfile_from_path(path);
			plugin.has_gui       = has_gui;
			m = DATA_->model.update(ez::nort, [plugin](model&& m) {
				m.plugins = m.plugins.insert(plugin);
				return m;
			});
			ui::on_plugin_scanned(ez::nort, plugin.id);
			retry_failed_devices(plugin, scanner->flags);
			return;
		}
		case scuff::plugin_type::vst3: {
			// Not implemented
			return;
		}
	}
}

static
auto stderr_respond(scan_::scanner*, const nlohmann::json& j) -> void {
	const std::string type = j["type"];
	if (type == "broken-plugfile") { read_broken_plugfile(j); return; }
	if (type == "broken-plugin")   { read_broken_plugin(j); return; }
}

static
auto stdout_respond(scan_::scanner* scanner, const nlohmann::json& j) -> void {
	const std::string type = j["type"];
	if (type == "plugfile") { read_plugfile(scanner, j); return; }
	if (type == "plugin")   { read_plugin(scanner, j); return; }
}

static
auto join(const std::vector<std::string>& strings) -> std::string {
	if (strings.empty()) {
		return {};
	}
	return std::accumulate(
		std::next(strings.begin()),
		strings.end(),
		strings[0],
		[](const std::string& a, const std::string& b) { return a + " " + b; });
}

static
auto report_exception(const std::vector<std::string>& args, const std::exception& err) -> void {
	ui::scan_error(ez::nort, std::format("{} (args: {})", err.what(), join(args)));
}

auto read_lines(scan_::scanner* scanner, const std::vector<std::string>& args, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred, respond_fn respond) -> void {
	if (ec) {
		return;
	}
	auto is   = std::istream{reader.buffer};
	auto line = std::string{};
	std::getline(is, line);
	if (!line.empty()) {
		try                               { respond(scanner, nlohmann::json::parse(line)); }
		catch (const std::exception& err) { report_exception(args, err); }
	}
	// continue reading
	basio::async_read_until(*reader.pipe, *reader.buffer, '\n', [scanner, args, reader, respond](const bsys::error_code& ec, size_t bytes_transferred) {
		read_lines(scanner, args, reader, ec, bytes_transferred, respond);
	});
}

static
auto read_stderr_lines(scan_::scanner* scanner, const std::vector<std::string>& args, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	read_lines(scanner, args, reader, ec, bytes_transferred, stderr_respond);
}

static
auto read_stdout_lines(scan_::scanner* scanner, const std::vector<std::string>& args, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	read_lines(scanner, args, reader, ec, bytes_transferred, stdout_respond);
}

static
auto scan_system_for_installed_plugins(scan_::scanner* scanner) -> void {
	if (!(std::filesystem::exists(scanner->exe_path) && std::filesystem::is_regular_file(scanner->exe_path))) {
		ui::error(ez::nort, std::format("Scanner executable not found: {}", scanner->exe_path));
		return;
	}
	const auto exe_args = make_exe_args_for_plugin_listing();
	const auto proc     = start_scanner_process(scanner, exe_args);
	async_read_lines(scanner, exe_args, proc.stderr_reader, read_stderr_lines);
	async_read_lines(scanner, exe_args, proc.stdout_reader, read_stdout_lines);
}

static
auto thread(std::stop_token token, std::string scan_exe_path, scan_flags flags) -> void {
	struct scope_exit_t { ~scope_exit_t() { DATA_->scanning = false; } } scope_exit;
	scan_::scanner scanner;
	scanner.exe_path = scan_exe_path;
	scanner.flags    = flags;
	ui::scan_started(ez::nort);
	basio::post(scanner.context, [&scanner] { scan_system_for_installed_plugins(&scanner); });
	while (!(token.stop_requested() || scanner.context.stopped())) {
		scanner.context.run_one();
	}
	ui::scan_complete(ez::nort);
}

static
auto start(std::string_view scan_exe_path, scan_flags flags) -> void {
	DATA_->scanning    = true;
	DATA_->scan_thread = std::jthread{scan_::thread, std::string{scan_exe_path}, flags};
}

static
auto stop_if_it_is_already_running() -> void {
	if (DATA_->scan_thread.joinable()) {
		DATA_->scan_thread.request_stop();
		DATA_->scan_thread.join();
	}
}

} // scan_
} // scuff
