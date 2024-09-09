#pragma once

#include "data.hpp"
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <nlohmann/json.hpp>

// MY APOLOGIES BUT I DO NOT UNDERSTAND HOW TO USE BOOST::ASIO CORRECTLY
// THIS SEEMS TO WORK
// IF YOU WANT TO REFACTOR THIS THEN GO AHEAD

namespace basio = boost::asio;
namespace bp    = boost::process;
namespace bsys  = boost::system;

namespace scuff {
namespace scan {

struct proc {
	static constexpr auto STDOUT = 0;
	static constexpr auto STDERR = 1;
	std::stop_token* stop_token;
	basio::io_context* ioctx;
	std::array<basio::streambuf, 2> buffers;
	std::array<bp::async_pipe, 2> pipes;
	bp::child child;
	proc(std::stop_token* stop_token, basio::io_context* ioctx, const std::string& exe, const std::vector<std::string>& args)
		: stop_token{stop_token}
		, ioctx{ioctx}
		, pipes{*ioctx, *ioctx}
		, child{os::start_child_process(exe, args, bp::std_out > pipes[STDOUT], bp::std_err > pipes[STDERR], *ioctx)}
	{
	}
};

struct reader {
	std::shared_ptr<proc> proc;
	basio::streambuf* buffer;
	bp::async_pipe* pipe;
	reader(std::shared_ptr<scan::proc> proc, size_t stream)
		: proc{proc}
		, buffer{&proc->buffers[stream]}
		, pipe{&proc->pipes[stream]}
	{
	}
};

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
auto async_read_lines(const std::shared_ptr<scan::reader>& reader, ReadFn&& read_fn) -> void {
	auto wrapper_fn = [reader, read_fn = std::forward<ReadFn>(read_fn)](const bsys::error_code& ec, size_t bytes_transferred) {
		if (reader->proc->stop_token->stop_requested()) {
			reader->proc->ioctx->stop();
			return;
		}
		read_fn(reader, ec, bytes_transferred);
	};
	basio::async_read_until(*reader->pipe, *reader->buffer, '\n', wrapper_fn);
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

static auto read_stderr_line(const std::shared_ptr<scan::reader>& reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;
static auto read_stdout_line(const std::shared_ptr<scan::reader>& reader, const bsys::error_code& ec, size_t bytes_transferred) -> void;

static
auto async_scan_clap_file(const std::shared_ptr<scan::reader>& reader, const std::string& path) -> void {
	const auto exe      = DATA_->scanner_exe_path;
	const auto exe_args = make_exe_args_for_scanning_plugin(path);
	auto proc           = std::make_shared<scan::proc>(reader->proc->stop_token, reader->proc->ioctx, exe, exe_args);
	auto stderr_reader  = std::make_shared<scan::reader>(proc, scan::proc::STDERR);
	auto stdout_reader  = std::make_shared<scan::reader>(proc, scan::proc::STDOUT);
	async_read_lines(stderr_reader, read_stderr_line);
	async_read_lines(stdout_reader, read_stdout_line);
}

static
auto read_plugfile(const std::shared_ptr<scan::reader>& reader, const nlohmann::json& j) -> void {
	const std::string plugfile_type = j["plugfile-type"];
	const std::string path          = j["path"];
	plugfile pf;
	pf.id   = id::plugfile{id_gen_++};
	pf.path = path;
	const auto model = DATA_->working_model.lock();
	model->plugfiles = model->plugfiles.insert(pf);
	DATA_->callbacks.on_plugfile_scanned.fn(&DATA_->callbacks.on_plugfile_scanned, pf.id.value);
	async_scan_clap_file(reader, path);
}

static
auto read_plugin(const nlohmann::json& j) -> void {
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
auto read_error(const nlohmann::json& j) -> void {
	const std::string type = j["type"];
	if (type == "broken-plugfile") { read_broken_plugfile(j); return; }
	if (type == "broken-plugin")   { read_broken_plugin(j); return; }
}

static
auto read_output(const std::shared_ptr<scan::reader>& reader, const nlohmann::json& j) -> void {
	const std::string type = j["type"];
	if (type == "plugfile") { read_plugfile(reader, j); return; }
	if (type == "plugin")   { read_plugin(j); return; }
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
auto read_stderr_line(const std::shared_ptr<scan::reader>& reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	if (ec) {
		return;
	}
	if (reader->proc->stop_token->stop_requested()) {
		reader->proc->ioctx->stop();
		return;
	}
	auto is   = std::istream{reader->buffer};
	auto line = std::string{};
	std::getline(is, line);
	if (!line.empty()) {
		try { read_error(nlohmann::json::parse(line)); }
		catch (const std::exception& err) {
			report_exception(err);
		}
	}
	// continue reading
	async_read_lines(reader, read_stderr_line);
}

static
auto read_stdout_line(const std::shared_ptr<scan::reader>& reader, const bsys::error_code& ec, size_t bytes_transferred) -> void {
	if (ec) {
		return;
	}
	if (reader->proc->stop_token->stop_requested()) {
		reader->proc->ioctx->stop();
		return;
	}
	auto is   = std::istream{reader->buffer};
	auto line = std::string{};
	std::getline(is, line);
	if (!line.empty()) {
		try { read_output(reader, nlohmann::json::parse(line)); }
		catch (const std::exception& err) {
			report_exception(err);
		}
	}
	// continue reading
	async_read_lines(reader, read_stdout_line);
}

static
auto scan_system_for_installed_plugins(std::stop_token* stop_token, basio::io_context* ioctx) -> void {
	const auto exe           = DATA_->scanner_exe_path;
	if (!(std::filesystem::exists(exe) && std::filesystem::is_regular_file(exe))) {
		const auto err = std::format("Scanner executable not found: {}", exe);
		report_error(err);
		return;
	}
	const auto exe_args      = make_exe_args_for_plugin_listing();
	const auto proc          = std::make_shared<scan::proc>(stop_token, ioctx, exe, exe_args);
	const auto stderr_reader = std::make_shared<scan::reader>(proc, scan::proc::STDERR);
	const auto stdout_reader = std::make_shared<scan::reader>(proc, scan::proc::STDOUT);
	async_read_lines(stderr_reader, read_stderr_line);
	async_read_lines(stdout_reader, read_stdout_line);
}

static
auto thread(std::stop_token stop_token) -> void {
	DATA_->callbacks.on_scan_started.fn(&DATA_->callbacks.on_scan_started);
	basio::io_context ioctx;
	basio::post(ioctx, [&stop_token, &ioctx]{ scan_system_for_installed_plugins(&stop_token, &ioctx); });
	ioctx.run();
	DATA_->callbacks.on_scan_complete.fn(&DATA_->callbacks.on_scan_complete);
}

static
auto start() -> void {
	DATA_->scanner_thread = std::jthread{scan::thread};
}

static
auto stop_if_it_is_already_running() -> void {
	if (DATA_->scanner_thread.joinable()) {
		DATA_->scanner_thread.request_stop();
		DATA_->scanner_thread.join();
	}
}

} // scan
} // scuff
