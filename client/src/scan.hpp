#pragma once

#include "common/os.hpp"
#include "data.hpp"
#include <boost/process.hpp>
#include <deque>
#include <nlohmann/json.hpp>

// MY APOLOGIES BUT I DO NOT UNDERSTAND HOW TO USE BOOST::ASIO CORRECTLY
// THIS SEEMS TO WORK
// IF YOU WANT TO REFACTOR THIS THEN GO AHEAD

namespace bp   = boost::process;
namespace bsys = boost::system;

namespace scuff {
namespace scan {

struct scanner {
	basio::io_context context;
	std::deque<basio::streambuf> buffers;
	std::deque<bp::async_pipe> pipes;
	std::deque<bp::child> procs;
};

struct reader {
	basio::streambuf* buffer;
	bp::async_pipe* pipe;
};

[[nodiscard]] static
auto add_buffer(scan::scanner* scanner) -> basio::streambuf* {
	scanner->buffers.emplace_back();
	return &scanner->buffers.back();
}

[[nodiscard]] static
auto add_pipe(scan::scanner* scanner) -> bp::async_pipe* {
	scanner->pipes.push_back(bp::async_pipe{scanner->context});
	return &scanner->pipes.back();
}

static
auto start_scanner_process(scan::scanner* scanner, const std::string& exe, const std::vector<std::string>& args, bp::async_pipe* pipe_out, bp::async_pipe* pipe_err) -> void {
	scanner->procs.emplace_back(os::start_child_process(exe, args, bp::std_out > *pipe_out, bp::std_err > *pipe_err, scanner->context));
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
auto async_read_lines(scan::scanner* scanner, scan::reader reader, ReadFn&& read_fn) -> void {
	auto wrapper_fn = [scanner, reader, read_fn = std::forward<ReadFn>(read_fn)](const bsys::error_code& ec, size_t bytes_transferred) {
		read_fn(scanner, reader, ec, bytes_transferred);
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
	const auto model = DATA_->working_model.lock();
	model->plugfiles = model->plugfiles.insert(pf);
	DATA_->callbacks.on_plugfile_broken.fn(&DATA_->callbacks.on_plugfile_broken, pf.id.value);
}

static
auto read_broken_plugin(const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	const std::string error         = j["error"];
	if (plugfile_type == "clap") {
		const std::string name    = j["name"];
		const std::string id      = j["id"];
		const std::string url     = j["url"];
		const std::string vendor  = j["vendor"];
		const std::string version = j["version"];
		scuff::plugin plugin;
		plugin.id      = id::plugin{id_gen_++};
		plugin.ext_id  = ext::id::plugin{id};
		plugin.name    = name;
		plugin.vendor  = vendor;
		plugin.version = version;
		const auto model = DATA_->working_model.lock();
		model->plugins = model->plugins.insert(plugin);
		DATA_->callbacks.on_plugin_broken.fn(&DATA_->callbacks.on_plugin_broken, plugin.id.value);
	}
}

static auto read_stderr_line(scan::scanner* scanner, scan::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;
static auto read_stdout_line(scan::scanner* scanner, scan::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;

static
auto async_scan_clap_file(scan::scanner* scanner, const std::string& path) -> void {
	const auto exe           = DATA_->scanner_exe_path;
	const auto exe_args      = make_exe_args_for_scanning_plugin(path);
	const auto stderr_buf    = add_buffer(scanner);
	const auto stdout_buf    = add_buffer(scanner);
	const auto stderr_pipe   = add_pipe(scanner);
	const auto stdout_pipe   = add_pipe(scanner);
	const auto stderr_reader = scan::reader{stderr_buf, stderr_pipe};
	const auto stdout_reader = scan::reader{stdout_buf, stdout_pipe};
	start_scanner_process(scanner, exe, exe_args, stdout_pipe, stderr_pipe);
	async_read_lines(scanner, stderr_reader, read_stderr_line);
	async_read_lines(scanner, stdout_reader, read_stdout_line);
}

static
auto read_plugfile(scan::scanner* scanner, const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	plugfile pf;
	pf.id   = id::plugfile{id_gen_++};
	pf.path = path;
	const auto model = DATA_->working_model.lock();
	model->plugfiles = model->plugfiles.insert(pf);
	DATA_->callbacks.on_plugfile_scanned.fn(&DATA_->callbacks.on_plugfile_scanned, pf.id.value);
	basio::post(scanner->context, [scanner, path] { async_scan_clap_file(scanner, path); });
}

static
auto read_plugin(scan::scanner*, const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	if (plugfile_type == "clap") {
		const std::string name    = j["name"];
		const std::string id      = j["id"];
		const std::string url     = j["url"];
		const std::string vendor  = j["vendor"];
		const std::string version = j["version"];
		scuff::plugin plugin;
		plugin.id      = id::plugin{id_gen_++};
		plugin.ext_id  = ext::id::plugin{id};
		plugin.name    = name;
		plugin.vendor  = vendor;
		plugin.version = version;
		const auto model = DATA_->working_model.lock();
		model->plugins = model->plugins.insert(plugin);
		DATA_->callbacks.on_plugin_scanned.fn(&DATA_->callbacks.on_plugin_scanned, plugin.id.value);
	}
}

static
auto read_error(scan::scanner*, const nlohmann::json& j) -> void {
	const std::string type = j["type"];
	if (type == "broken-plugfile") { read_broken_plugfile(j); return; }
	if (type == "broken-plugin")   { read_broken_plugin(j); return; }
}

static
auto read_output(scan::scanner* scanner, const nlohmann::json& j) -> void {
	const std::string type = j["type"];
	if (type == "plugfile") { read_plugfile(scanner, j); return; }
	if (type == "plugin")   { read_plugin(scanner, j); return; }
}

static
auto report_error(std::string_view err) -> void {
	DATA_->callbacks.on_scan_error.fn(&DATA_->callbacks.on_scan_error, err.data());
}

static
auto report_exception(const std::exception& err) -> void {
	report_error(err.what());
}

static
auto read_stderr_line(scan::scanner* scanner, scan::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	if (ec) {
		return;
	}
	auto is   = std::istream{reader.buffer};
	auto line = std::string{};
	std::getline(is, line);
	if (!line.empty()) {
		try { read_error(scanner, nlohmann::json::parse(line)); }
		catch (const std::exception& err) {
			report_exception(err);
		}
	}
	// continue reading
	async_read_lines(scanner, reader, read_stderr_line);
}

static
auto read_stdout_line(scan::scanner* scanner, scan::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	if (ec) {
		return;
	}
	auto is   = std::istream{reader.buffer};
	auto line = std::string{};
	std::getline(is, line);
	if (!line.empty()) {
		try { read_output(scanner, nlohmann::json::parse(line)); }
		catch (const std::exception& err) {
			report_exception(err);
		}
	}
	// continue reading
	async_read_lines(scanner, reader, read_stdout_line);
}

static
auto scan_system_for_installed_plugins(scan::scanner* scanner) -> void {
	const auto exe = DATA_->scanner_exe_path;
	if (!(std::filesystem::exists(exe) && std::filesystem::is_regular_file(exe))) {
		const auto err = std::format("Scanner executable not found: {}", exe);
		report_error(err);
		return;
	}
	const auto exe_args      = make_exe_args_for_plugin_listing();
	const auto stderr_buf    = add_buffer(scanner);
	const auto stdout_buf    = add_buffer(scanner);
	const auto stderr_pipe   = add_pipe(scanner);
	const auto stdout_pipe   = add_pipe(scanner);
	const auto stderr_reader = scan::reader{stderr_buf, stderr_pipe};
	const auto stdout_reader = scan::reader{stdout_buf, stdout_pipe};
	start_scanner_process(scanner, exe, exe_args, stdout_pipe, stderr_pipe);
	async_read_lines(scanner, stderr_reader, read_stderr_line);
	async_read_lines(scanner, stdout_reader, read_stdout_line);
}

static
auto thread(std::stop_token token) -> void {
	scan::scanner scanner;
	DATA_->callbacks.on_scan_started.fn(&DATA_->callbacks.on_scan_started);
	basio::post(scanner.context, [&scanner] { scan_system_for_installed_plugins(&scanner); });
	while (!(token.stop_requested() || scanner.context.stopped())) {
		scanner.context.run_one();
	}
	DATA_->callbacks.on_scan_complete.fn(&DATA_->callbacks.on_scan_complete);
}

[[nodiscard]] static
auto is_running() -> bool {
	return DATA_->scan_thread.joinable();
}

static
auto start() -> void {
	DATA_->scan_thread = std::jthread{scan::thread};
}

static
auto stop_if_it_is_already_running() -> void {
	if (DATA_->scan_thread.joinable()) {
		DATA_->scan_thread.request_stop();
		DATA_->scan_thread.join();
	}
}

} // scan
} // scuff
