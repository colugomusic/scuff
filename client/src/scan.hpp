#pragma once

#include "common/os.hpp"
#include "data.hpp"
#include <boost/process.hpp>
#include <deque>
#include <nlohmann/json.hpp>

namespace bp   = boost::process;
namespace bsys = boost::system;

namespace scuff {
namespace scan_ {

struct scanner {
	basio::io_context context;
	std::string_view exe_path;
	std::deque<basio::streambuf> buffers;
	std::deque<bp::async_pipe> pipes;
	std::deque<bp::child> procs;
	int flags = 0;
};

struct reader {
	basio::streambuf* buffer;
	bp::async_pipe* pipe;
};

struct scanner_process {
	reader stderr_reader;
	reader stdout_reader;
};

using respond_fn = auto(*)(scan_::scanner* scanner, const nlohmann::json& j) -> void;

static auto read_stderr_lines(scan_::scanner* scanner, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;
static auto read_stdout_lines(scan_::scanner* scanner, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;

[[nodiscard]] static
auto add_buffer(scan_::scanner* scanner) -> basio::streambuf* {
	scanner->buffers.emplace_back();
	return &scanner->buffers.back();
}

[[nodiscard]] static
auto add_pipe(scan_::scanner* scanner) -> bp::async_pipe* {
	scanner->pipes.push_back(bp::async_pipe{scanner->context});
	return &scanner->pipes.back();
}

static
auto start_scanner_process(scan_::scanner* scanner, const std::vector<std::string>& args, bp::async_pipe* pipe_out, bp::async_pipe* pipe_err) -> void {
	scanner->procs.emplace_back(os::start_child_process(scanner->exe_path.data(), args, bp::std_out > *pipe_out, bp::std_err > *pipe_err, scanner->context));
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
auto async_read_lines(scan_::scanner* scanner, scan_::reader reader, ReadFn&& read_fn) -> void {
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
	auto m = DATA_->model.lock_read();
	m.plugfiles = m.plugfiles.insert(std::move(pf));
	DATA_->model.lock_write(m);
	DATA_->callbacks.on_plugfile_broken(pf.id);
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
		auto m = DATA_->model.lock_read();
		m.plugins = m.plugins.insert(std::move(plugin));
		DATA_->model.lock_write(m);
		DATA_->callbacks.on_plugin_broken(plugin.id);
	}
}

static
auto async_scan_clap_file(scan_::scanner* scanner, const std::string& path) -> void {
	const auto exe_args = make_exe_args_for_scanning_plugin(path);
	const auto proc     = start_scanner_process(scanner, exe_args);
	async_read_lines(scanner, proc.stderr_reader, read_stderr_lines);
	async_read_lines(scanner, proc.stdout_reader, read_stdout_lines);
}

static
auto read_plugfile(scan_::scanner* scanner, const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	plugfile pf;
	pf.id   = id::plugfile{id_gen_++};
	pf.path = path;
	auto m = DATA_->model.lock_read();
	m.plugfiles = m.plugfiles.insert(std::move(pf));
	DATA_->model.lock_write(m);
	DATA_->callbacks.on_plugfile_scanned(pf.id);
	basio::post(scanner->context, [scanner, path] { async_scan_clap_file(scanner, path); });
}

static
auto read_plugin(scan_::scanner*, const nlohmann::json& j) -> void {
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
		auto m = DATA_->model.lock_read();
		m.plugins = m.plugins.insert(std::move(plugin));
		DATA_->model.lock_write(m);
		DATA_->callbacks.on_plugin_scanned(plugin.id);
		// TODO: if flags & scuff_scan_flag_reload_failed_devices,
		//       reload any unloaded devices which use this plugin 
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
auto report_error(std::string_view err) -> void {
	DATA_->callbacks.on_scan_error(err.data());
}

static
auto report_exception(const std::exception& err) -> void {
	report_error(err.what());
}

auto read_lines(scan_::scanner* scanner, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred, respond_fn respond) -> void {
	if (ec) {
		return;
	}
	auto is   = std::istream{reader.buffer};
	auto line = std::string{};
	std::getline(is, line);
	if (!line.empty()) {
		try                               { respond(scanner, nlohmann::json::parse(line)); }
		catch (const std::exception& err) { report_exception(err); }
	}
	// continue reading
	basio::async_read_until(*reader.pipe, *reader.buffer, '\n', [scanner, reader, respond](const bsys::error_code& ec, size_t bytes_transferred) {
		read_lines(scanner, reader, ec, bytes_transferred, respond);
	});
}

static
auto read_stderr_lines(scan_::scanner* scanner, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	read_lines(scanner, reader, ec, bytes_transferred, stderr_respond);
}

static
auto read_stdout_lines(scan_::scanner* scanner, scan_::reader reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	read_lines(scanner, reader, ec, bytes_transferred, stdout_respond);
}

static
auto scan_system_for_installed_plugins(scan_::scanner* scanner) -> void {
	if (!(std::filesystem::exists(scanner->exe_path) && std::filesystem::is_regular_file(scanner->exe_path))) {
		const auto err = std::format("Scanner executable not found: {}", scanner->exe_path);
		report_error(err);
		return;
	}
	const auto exe_args = make_exe_args_for_plugin_listing();
	const auto proc     = start_scanner_process(scanner, exe_args);
	async_read_lines(scanner, proc.stderr_reader, read_stderr_lines);
	async_read_lines(scanner, proc.stdout_reader, read_stdout_lines);
}

static
auto thread(std::stop_token token, std::string scan_exe_path, int flags) -> void {
	scan_::scanner scanner;
	scanner.exe_path = scan_exe_path;
	scanner.flags    = flags;
	DATA_->callbacks.on_scan_started();
	basio::post(scanner.context, [&scanner] { scan_system_for_installed_plugins(&scanner); });
	while (!(token.stop_requested() || scanner.context.stopped())) {
		scanner.context.run_one();
	}
	DATA_->callbacks.on_scan_complete();
}

[[nodiscard]] static
auto is_running() -> bool {
	return DATA_->scan_thread.joinable();
}

static
auto start(const char* scan_exe_path, int flags) -> void {
	DATA_->scan_thread = std::jthread{scan_::thread, scan_exe_path, flags};
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
