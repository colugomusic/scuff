#include "common-clap.hpp"
#include "common-plugin-type.hpp"
#include "common-util.hpp"
#include <clap/factory/plugin-factory.h>
#include <clap/string-sizes.h>
#include <cxxopts.hpp>
#include <flux.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include <flux.hpp>
#include <vector>

namespace scanner {

template <typename ClassType, typename T, typename... Args>
concept IsMemberFn = requires(ClassType obj, T t, Args... args) {
	(obj.*t)(args...);
};

struct stream_redirector {
	stream_redirector(FILE* stream) : stream{stream}, old{scuff::os::redirect_stream(stream)} {}
	~stream_redirector() { scuff::os::restore_stream(stream, old); }
	template <typename ClassType, typename Fn, typename... Args>
	requires IsMemberFn<ClassType, Fn, Args...>
	static
	auto invoke(ClassType&& obj, Fn&& fn, Args&&... args) -> decltype(auto) {
		stream_redirector redirect_stdout{stdout};
		stream_redirector redirect_stderr{stderr};
		return (obj.*fn)(args...);
	}
private:
	FILE* stream;
	int old;
};

struct plugfile {
	scuff::plugin_type type;
	std::filesystem::path path;
};

struct options {
	std::vector<std::filesystem::path> additional_search_paths;
	std::string file_to_scan;
};

[[nodiscard]] static
auto parse_options(int argc, const char* argv[]) -> options {
	auto options_parser = cxxopts::Options("scuff-scanner", "Scans the system for installed CLAP/VST plugins");
	options_parser.add_options()
		("f,file", "Plugin file to scan", cxxopts::value<std::string>())
		("s,search-paths", "List of additional search paths, separated by ';'", cxxopts::value<std::string>())
		;
	auto result = options_parser.parse(argc, argv);
	auto options = scanner::options{};
	if (result.count("search-paths") > 0) {
		options.additional_search_paths =
			flux::ref(result["search-paths"].as<std::string>())
				.split_string(';')
				.map([](const auto& s) { return std::filesystem::path(s); })
				.to<std::vector<std::filesystem::path>>();
	}
	if (result.count("file") > 0) {
		options.file_to_scan = result["file"].as<std::string>();
	}
	return options;
}

[[nodiscard]] static
auto get_plugfile_search_paths(const options& opts) -> std::vector<std::filesystem::path> {
	auto system_search_paths     = scuff::os::get_system_search_paths();
	auto additional_search_paths = opts.additional_search_paths;
	auto paths =
		flux::chain(flux::ref(system_search_paths), flux::ref(additional_search_paths))
			.to<std::vector<std::filesystem::path>>();
	flux::sort(paths, std::less{});
	return flux::ref(paths)
		.dedup()
		.to<std::vector<std::filesystem::path>>();
}

[[nodiscard]] static
auto to_plugfile(const std::filesystem::path& path) -> std::optional<plugfile> {
	if (scuff::os::is_clap_file(path)) { return plugfile{scuff::plugin_type::clap, path}; }
	if (scuff::os::is_vst3_file(path)) { return plugfile{scuff::plugin_type::vst3, path}; }
	return std::nullopt;
}

[[nodiscard]] static
auto find_plugfiles(const std::filesystem::path& search_path) -> std::vector<plugfile> {
	std::vector<plugfile> plugfiles;
	try {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(search_path)) {
			if (const auto pf = to_plugfile(entry.path())) {
				plugfiles.push_back(*pf);
			}
		}
	} catch (...) {}
	return plugfiles;
}

static
auto add(nlohmann::json* j, const plugfile& pf) -> void {
	(*j)["plugfile-type"] = scuff::to_string(pf.type);
	(*j)["path"]          = pf.path;
}

[[nodiscard]] static
auto has_null_within_allowed_size(const char* str, size_t sz) -> bool {
	for (int i = 0; i < sz; ++i) {
		if (str[i] == 0) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] static
auto get_features(const clap_plugin_descriptor& desc) -> std::vector<std::string> {
	std::vector<std::string> out;
	auto f = desc.features;
	while (*f) {
		if (!has_null_within_allowed_size(*f, CLAP_NAME_SIZE)) {
			break;
		}
		out.push_back(*f);
		f++;
	}
	return out;
}

static
auto add(nlohmann::json* j, const clap_plugin_descriptor& desc) -> void {
	(*j)["name"]     = desc.name;
	(*j)["id"]       = desc.id;
	(*j)["url"]      = desc.url;
	(*j)["vendor"]   = desc.vendor;
	(*j)["version"]  = desc.version;
	(*j)["features"] = get_features(desc);
}

static
auto report_broken_plugfile(const plugfile& pf, std::string_view err) -> void {
	nlohmann::json j;
	j["type"]  = "broken-plugfile";
	j["error"] = err;
	add(&j, pf);
	fprintf(stderr, "%s\n", j.dump().c_str());
}

static
auto report_broken_plugin(const plugfile& pf, const clap_plugin_descriptor& desc, std::string_view err) -> void {
	nlohmann::json j;
	j["type"]  = "broken-plugin";
	j["error"] = err;
	add(&j, desc);
	add(&j, pf);
	fprintf(stderr, "%s\n", j.dump().c_str());
}

static
auto report_plugfile(const plugfile& pf) -> void {
	nlohmann::json j;
	j["type"] = "plugfile";
	add(&j, pf);
	fprintf(stdout, "%s\n", j.dump().c_str());
}

static
auto report_plugin(const plugfile& pf, const clap_plugin_descriptor& desc, bool has_gui, bool has_params) -> void {
	nlohmann::json j;
	j["type"]       = "plugin";
	j["has-gui"]    = has_gui;
	j["has-params"] = has_params;
	add(&j, desc);
	add(&j, pf);
	fprintf(stdout, "%s\n", j.dump().c_str());
}

static
auto scan_clap_plugfile_safe(const plugfile& pf) -> void {
	const auto entry = scuff::os::find_clap_entry(pf.path);
	if (!entry) {
		report_broken_plugfile(pf, "Couldn't resolve clap_entry");
		return;
	}
	report_plugfile(pf);
}

static
auto scan_vst3_plugfile_safe(const plugfile& pf) -> void {
	// Not implemented
}

static auto clap_host_get_extension(const clap_host* host, const char* id) -> const void* { return nullptr; }
static auto clap_host_request_callback(const clap_host* host) -> void {}
static auto clap_host_request_process(const clap_host* host) -> void {}
static auto clap_host_request_restart(const clap_host* host) -> void {}

static
auto scan_clap_plugin(const plugfile& pf, const clap_plugin_factory_t& factory, uint32_t index) -> void {
	const auto desc = factory.get_plugin_descriptor(&factory, index);
	if (!desc) {
		return;
	}
	clap_host host = {0};
	host.clap_version     = CLAP_VERSION_INIT;
	host.get_extension    = clap_host_get_extension;
	host.name             = "scuff-scanner";
	host.request_callback = clap_host_request_callback;
	host.request_process  = clap_host_request_process;
	host.request_restart  = clap_host_request_restart;
	host.url              = "https://github.com/colugomusic/scuff";
	host.vendor           = "Moron Enterprises";
	host.version          = "0.0.0";
	const auto device = stream_redirector::invoke(factory, &clap_plugin_factory_t::create_plugin, &factory, &host, desc->id);
	if (!device) {
		report_broken_plugin(pf, *desc, "clap_plugin_factory.create_plugin failed");
		return;
	}
	if (!stream_redirector::invoke(*device, &clap_plugin_t::init, device)) {
		report_broken_plugin(pf, *desc, "clap_plugin.init failed");
		device->destroy(device);
		return;
	}
	if (!stream_redirector::invoke(*device, &clap_plugin_t::activate, device, 48000, 32, 4096)) {
		report_broken_plugin(pf, *desc, "clap_plugin.activate failed");
		device->destroy(device);
		return;
	}
	const auto has_gui    = scuff::has_gui(*device);
	const auto has_params = scuff::has_params(*device);
	report_plugin(pf, *desc, has_gui, has_params);
	device->deactivate(device);
	device->destroy(device);
}	

static
auto scan_clap_plugfile_full(const plugfile& pf) -> void {
	const auto entry = scuff::os::find_clap_entry(pf.path);
	if (!entry) {
		report_broken_plugfile(pf, "Couldn't resolve clap_entry");
		return;
	}
	if (!entry->init(pf.path.string().c_str())) {
		report_broken_plugfile(pf, "clap_plugin_entry.init failed");
		return;
	}
	const auto factory = reinterpret_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
	if (!factory) {
		report_broken_plugfile(pf, "clap_plugin_entry.get_factory failed");
		entry->deinit();
		return;
	}
	const auto plugin_count = factory->get_plugin_count(factory);
	for (uint32_t i = 0; i < plugin_count; i++) {
		scan_clap_plugin(pf, *factory, i);
	}
	entry->deinit();
}

static
auto scan_vst3_plugfile_full(const plugfile& pf) -> void {
	// Not implemented
}

static
auto scan_plugfile_full(const options& opts) -> void {
	if (const auto pf = to_plugfile(opts.file_to_scan)) {
		switch (pf->type) {
			case scuff::plugin_type::clap: { scan_clap_plugfile_full(*pf); break; }
			case scuff::plugin_type::vst3: { scan_vst3_plugfile_full(*pf); break; }
			default:                       { break; }
		}
		return;
	}
	report_broken_plugfile({scuff::plugin_type::unknown, opts.file_to_scan}, "This doesn't look like a real plugin file.");
}

static
auto scan_plugfile_safe(const plugfile& pf) -> void {
	switch (pf.type) {
		case scuff::plugin_type::clap: { scan_clap_plugfile_safe(pf); break; }
		case scuff::plugin_type::vst3: { scan_vst3_plugfile_safe(pf); break; }
		default:                       { break; }
	}
}

static
auto scan_system_for_plugfiles(const options& opts) -> void {
	flux::from(get_plugfile_search_paths(opts))
		.map(find_plugfiles)
		.flatten()
		.for_each(scan_plugfile_safe);
}

} // scanner

auto main(int argc, const char* argv[]) -> int {
	try {
		auto options = scanner::parse_options(argc, argv);
		if (!options.file_to_scan.empty()) {
			scanner::scan_plugfile_full(options);
		}
		else {
			scanner::scan_system_for_plugfiles(options);
		}
	} catch (const std::exception& e) {
		std::fprintf(stderr, "Error: %s\n", e.what());
		return 1;
	}
	return 0;
}