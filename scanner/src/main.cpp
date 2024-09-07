#include "os.hpp"
#include "util.hpp"
#include <clap/factory/plugin-factory.h>
#include <cxxopts.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include <flux.hpp>
#include <vector>

namespace scanner {

enum class plugfile_type { unknown, clap, possible_vst2, vst3 };

struct plugfile {
	plugfile_type type;
	std::filesystem::path path;
};

struct options {
	std::vector<std::filesystem::path> additional_search_paths;
	std::string file_to_scan;
};

[[nodiscard]] static
auto to_string(plugfile_type type) -> std::string {
	switch (type) {
		case plugfile_type::clap:          { return "clap"; }
		case plugfile_type::vst3:          { return "vst3"; }
		case plugfile_type::possible_vst2: { return "possible_vst2"; }
		default:                           { return "unknown"; }
	}
}

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
	auto system_search_paths     = os::get_system_search_paths();
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
	if (os::is_clap_file(path)) {
		return plugfile{plugfile_type::clap, path};
	}
	if (os::is_vst3_file(path)) {
		return plugfile{plugfile_type::vst3, path};
	}
	if (os::could_be_a_vst2_file(path)) {
		return plugfile{plugfile_type::possible_vst2, path};
	}
	return std::nullopt;
}

[[nodiscard]] static
auto find_plugfiles(const std::filesystem::path& search_path) -> std::vector<plugfile> {
	std::vector<plugfile> plugfiles;
	try {
		for (const auto& entry : std::filesystem::directory_iterator(search_path)) {
			if (const auto pf = to_plugfile(entry.path())) {
				plugfiles.push_back(*pf);
			}
		}
	} catch (...) {}
	return plugfiles;
}

static
auto add(nlohmann::json* j, const plugfile& pf) -> void {
	(*j)["plugfile_type"] = to_string(pf.type);
	(*j)["path"]          = pf.path;
}

static
auto add(nlohmann::json* j, const clap_plugin_descriptor& desc) -> void {
	(*j)["name"]        = desc.name;
	(*j)["id"]          = desc.id;
	(*j)["url"]         = desc.url;
	(*j)["vendor"]      = desc.vendor;
	(*j)["version"]     = desc.version;
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
auto report_plugin(const plugfile& pf, const clap_plugin_descriptor& desc) -> void {
	nlohmann::json j;
	j["type"] = "plugin";
	add(&j, desc);
	add(&j, pf);
	fprintf(stdout, "%s\n", j.dump().c_str());
}

static
auto scan_clap_plugfile_safe(const plugfile& pf) -> void {
	const auto entry = os::find_clap_entry(pf.path);
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

static
auto scan_possible_vst2_plugfile_safe(const plugfile& pf) -> void {
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
	const auto device = factory.create_plugin(&factory, &host, desc->id);
	if (!device) {
		report_broken_plugin(pf, *desc, "clap_plugin_factory.create_plugin failed");
		return;
	}
	if (!device->init(device)) {
		report_broken_plugin(pf, *desc, "clap_plugin.init failed");
		device->destroy(device);
		return;
	}
	if (!device->activate(device, 48000, 32, 4096)) {
		report_broken_plugin(pf, *desc, "clap_plugin.activate failed");
		device->destroy(device);
		return;
	}
	report_plugin(pf, *desc);
	device->deactivate(device);
	device->destroy(device);
}	

static
auto scan_clap_plugfile_full(const plugfile& pf) -> void {
	const auto entry = os::find_clap_entry(pf.path);
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
auto scan_possible_vst2_plugfile_full(const plugfile& pf) -> void {
	// Not implemented
}

static
auto scan_plugfile_full(const options& opts) -> void {
	if (const auto pf = to_plugfile(opts.file_to_scan)) {
		switch (pf->type) {
			case plugfile_type::clap:          { scan_clap_plugfile_full(*pf); break; }
			case plugfile_type::vst3:          { scan_vst3_plugfile_full(*pf); break; }
			case plugfile_type::possible_vst2: { scan_possible_vst2_plugfile_full(*pf); break; }
			default:                           { break; }
		}
		return;
	}
	report_broken_plugfile({plugfile_type::unknown, opts.file_to_scan}, "This doesn't look like a real plugin file.");
}

static
auto scan_plugfile_safe(const plugfile& pf) -> void {
	switch (pf.type) {
		case plugfile_type::clap:          { scan_clap_plugfile_safe(pf); break; }
		case plugfile_type::vst3:          { scan_vst3_plugfile_safe(pf); break; }
		case plugfile_type::possible_vst2: { scan_possible_vst2_plugfile_safe(pf); break; }
		default:                           { break; }
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