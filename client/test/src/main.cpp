#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include <boost/program_options.hpp>
#include <filesystem>
#include <scuff/client.hpp>
#include <scuff/managed.hpp>
#include <vector>

namespace fs = std::filesystem;
namespace po = boost::program_options;

static auto sbox_exe_path_ = fs::path{SBOX_EXE_PATH};
static auto scan_exe_path_ = fs::path{SCAN_EXE_PATH};

auto setup(int argc, const char* argv[]) -> void {
	auto desc = po::options_description{"Allowed options"};
	desc.add_options()
		("sbox", po::value<fs::path>(&sbox_exe_path_), "path to the sandbox executable")
		("scan", po::value<fs::path>(&scan_exe_path_), "path to the scanner executable")
		;
	auto vm             = po::variables_map{};
	auto parsed_options = po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();
	po::store(parsed_options, vm);
	po::notify(vm);
	scuff::init();
	scuff::scan(scan_exe_path_.string(), {});
}

auto fatal(std::string_view err) -> int {
	std::cerr << err << std::endl;
	return EXIT_FAILURE;
}

auto go(int argc, const char* argv[]) -> int {
	setup(argc, argv);
	auto dt = doctest::Context{argc, argv};
	auto result = dt.run();
	scuff::shutdown();
	return result;
}

auto main(int argc, const char* argv[]) -> int {
	try                               { go(argc, argv); }
	catch (const std::exception& err) { return fatal(err.what()); }
	catch (...)                       { return fatal("Unknown error"); }
}

auto make_ui_reporter() -> scuff::general_ui {
	scuff::general_ui ui;
	ui.on_error = [](std::string_view err) {
		MESSAGE("error: ", err);
	};
	ui.on_plugfile_broken = [](scuff::id::plugfile pf) {
		const auto path = std::string_view{scuff::get_path(pf)};
		MESSAGE("broken plugfile: ", path);
	};
	ui.on_plugfile_scanned = [](scuff::id::plugfile pf) {
		const auto path = std::string_view{scuff::get_path(pf)};
		MESSAGE("working plugfile: ", path);
	};
	ui.on_plugin_broken = [](scuff::id::plugin plugin) {
		const auto name = std::string_view{scuff::get_name(plugin)};
		MESSAGE("broken plugin: ", name);
	};
	ui.on_plugin_scanned = [](scuff::id::plugin plugin) {
		const auto name = std::string_view{scuff::get_name(plugin)};
		MESSAGE("working plugin: ", name);
	};
	ui.on_scan_complete = [] {};
	ui.on_scan_error = [](std::string_view err) {
		MESSAGE("scan error: ", err);
	};
	ui.on_scan_started = [] {};
	ui.on_scan_warning = [](std::string_view warning) {
		MESSAGE("scan warning: ", warning);
	};
	return ui;
}

TEST_CASE("finish scanning") {
	bool done = false;
	scuff::general_ui ui;
	ui.on_error = [](std::string_view err) {
		INFO("error: ", err);
	};
	ui.on_plugfile_broken = [](scuff::id::plugfile pf) {
		const auto path = std::string_view{scuff::get_path(pf)};
		INFO("broken plugfile: ", path);
	};
	ui.on_plugfile_scanned = [](scuff::id::plugfile pf) {
		const auto path = std::string_view{scuff::get_path(pf)};
		INFO("working plugfile: ", path);
	};
	ui.on_plugin_broken = [](scuff::id::plugin plugin) {
		const auto name = std::string_view{scuff::get_name(plugin)};
		INFO("broken plugin: ", name);
	};
	ui.on_plugin_scanned = [](scuff::id::plugin plugin) {
		const auto name = std::string_view{scuff::get_name(plugin)};
		INFO("working plugin: ", name);
	};
	ui.on_scan_complete = [&done] { done = true; };
	ui.on_scan_error = [](std::string_view err) {
		INFO("scan error: ", err);
	};
	ui.on_scan_started = [] {};
	ui.on_scan_warning = [](std::string_view warning) {
		INFO("scan warning: ", warning);
	};
	while (!done) {
		scuff::ui_update(ui);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

auto create_a_bunch_of_devices(scuff::id::sandbox sbox) -> std::vector<scuff::id::device> {
	auto devices = std::vector<scuff::id::device>{};
	const auto plugins = scuff::get_working_plugins();
	size_t i = 0;
	for (const auto plugin : plugins) {
		const auto type = scuff::get_type(plugin);
		const auto ext_id = scuff::get_ext_id(plugin);
		const auto pf     = scuff::get_plugfile(plugin);
		const auto path   = scuff::get_path(pf);
		MESSAGE(i);
		INFO("creating device: ", path, " id: ", ext_id.value, " index: ", i);
		const auto device = scuff::create_device(sbox, type, ext_id);
		if (!device.was_created_successfully) {
			scuff::ui_update(make_ui_reporter());
		}
		REQUIRE(device.was_created_successfully);
		devices.push_back(device.id);
		i++;
	}
	return devices;
}

TEST_CASE("studio.kx.distrho.MaGigaverb") {
	const auto group  = scuff::managed_group{scuff::create_group(nullptr)};
	const auto sbox   = scuff::managed_sandbox{scuff::create_sandbox(group.id(), sbox_exe_path_.string())};
	const auto ext_id = scuff::ext::id::plugin{"studio.kx.distrho.MaGigaverb"};
	const auto plugin = scuff::find({ext_id});
	for (int i = 0; i < 10; i++) {
		const auto device = scuff::create_device(sbox.id(), scuff::plugin_type::clap, ext_id);
		if (!device.was_created_successfully) {
			MESSAGE(i);
		}
		REQUIRE(device.was_created_successfully);
		const auto managed = scuff::managed_device{device.id};
	}
}

TEST_CASE("com.FabFilter.preset-discovery.Saturn.2") {
	// This plugin has a lot of parameters
	const auto group  = scuff::managed_group{scuff::create_group(nullptr)};
	const auto sbox   = scuff::managed_sandbox{scuff::create_sandbox(group.id(), sbox_exe_path_.string())};
	const auto ext_id = scuff::ext::id::plugin{"com.FabFilter.preset-discovery.Saturn.2"};
	const auto plugin = scuff::find({ext_id});
	const auto device = scuff::create_device(sbox.id(), scuff::plugin_type::clap, ext_id);
	REQUIRE(device.was_created_successfully);
}

TEST_CASE("stress test") {
	auto group = scuff::managed_group{scuff::create_group(nullptr)};
	auto sbox    = scuff::managed_sandbox{scuff::create_sandbox(group.id(), sbox_exe_path_.string())};
	auto plugins = scuff::get_working_plugins();
	scuff::activate(group.id(), 44100.0);
	INFO("plugin count: ", plugins.size());
	//auto devices = create_a_bunch_of_devices(sbox.id());
	group = {};
	MESSAGE("creating device in group: ", group.id().value, " sbox: ", sbox.id().value);
	auto device = scuff::create_device(sbox.id(), scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"});
	REQUIRE(!device.was_created_successfully);
	group = scuff::managed_group{scuff::create_group(nullptr)};
	sbox = scuff::managed_sandbox{scuff::create_sandbox(group.id(), sbox_exe_path_.string())};
	MESSAGE("creating device in group: ", group.id().value, " sbox: ", sbox.id().value);
	device = scuff::create_device(sbox.id(), scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"});
	REQUIRE(device.was_created_successfully);
	group = scuff::managed_group{scuff::create_group(nullptr)};
	sbox = scuff::managed_sandbox{scuff::create_sandbox(group.id(), sbox_exe_path_.string())};
	group = {};
	MESSAGE("creating device in group: ", group.id().value, " sbox: ", sbox.id().value);
	device = scuff::create_device(sbox.id(), scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"});
	REQUIRE(!device.was_created_successfully);
	group = scuff::managed_group{scuff::create_group(nullptr)};
	sbox = scuff::managed_sandbox{scuff::create_sandbox(group.id(), sbox_exe_path_.string())};
	MESSAGE("creating device in group: ", group.id().value, " sbox: ", sbox.id().value);
	device = scuff::create_device(sbox.id(), scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"});
	REQUIRE(device.was_created_successfully);
}

TEST_CASE("stress test 2") {
	auto group = scuff::managed_group{scuff::create_group(nullptr)};
	const auto sbox    = scuff::managed_sandbox{scuff::create_sandbox(group.id(), sbox_exe_path_.string())};
	const auto plugins = scuff::get_working_plugins();
	scuff::activate(group.id(), 44100.0);
	INFO("plugin count: ", plugins.size());
	SUBCASE ("erase the group") {
		group = {};
	}
	SUBCASE ("deactivate group") {
		scuff::deactivate(group.id());
	}
	auto devices = create_a_bunch_of_devices(sbox.id());
	SUBCASE ("just erase the group") {
		group = {};
	}
	SUBCASE ("erase devices") {
		SUBCASE ("deactivate group first") {
			scuff::deactivate(group.id());
		}
		for (const auto dev : devices) {
			scuff::erase(dev);
		}
	}
}
