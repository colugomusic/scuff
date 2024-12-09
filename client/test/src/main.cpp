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

auto make_empty_group_reporter() -> scuff::group_ui {
	scuff::group_ui ui;
	ui.on_device_editor_visible_changed = [](scuff::id::device dev, bool visible, int64_t native_handle) {};
	ui.on_device_late_create = [](scuff::create_device_result result) {};
	ui.on_device_state_load = [](scuff::load_device_result result) {};
	ui.on_device_params_changed = [](scuff::id::device dev) {};
	ui.on_error = [](std::string_view err) {};
	ui.on_sbox_crashed = [](scuff::id::sandbox sbox, std::string_view error) {};
	ui.on_sbox_error = [](scuff::id::sandbox sbox, std::string_view error) {};
	ui.on_sbox_info = [](scuff::id::sandbox sbox, std::string_view info) {};
	ui.on_sbox_started = [](scuff::id::sandbox sbox) {};
	ui.on_sbox_warning = [](scuff::id::sandbox sbox, std::string_view warning) {};
	return ui;

}

auto make_empty_ui_reporter() -> scuff::general_ui {
	scuff::general_ui ui;
	ui.on_error = [](std::string_view err) {};
	ui.on_plugfile_broken = [](scuff::id::plugfile pf) {};
	ui.on_plugfile_scanned = [](scuff::id::plugfile pf) {};
	ui.on_plugin_broken = [](scuff::id::plugin plugin) {};
	ui.on_plugin_scanned = [](scuff::id::plugin plugin) {};
	ui.on_scan_complete = [] {};
	ui.on_scan_error = [](std::string_view err) {};
	ui.on_scan_started = [] {};
	ui.on_scan_warning = [](std::string_view warning) {};
	return ui;
}

TEST_CASE("reload failed device") {
	scuff::id::group group_id;
	scuff::id::sandbox sbox_id;
	scuff::create_device_result device;
	const auto ext_id = scuff::ext::id::plugin{"studio.kx.distrho.MaGigaverb"};
	REQUIRE_NOTHROW(group_id = scuff::create_group(nullptr));
	REQUIRE_NOTHROW(sbox_id = scuff::create_sandbox(group_id, sbox_exe_path_.string()));
	REQUIRE_NOTHROW(device = scuff::create_device(sbox_id, scuff::plugin_type::clap, ext_id));
	REQUIRE(!device.success);
	REQUIRE(!scuff::was_created_successfully(device.id));
	REQUIRE_NOTHROW(scuff::scan(scan_exe_path_.string(), {scuff::scan_flags::retry_failed_devices}));
	bool scanning_done = false;
	bool device_late_created = false;
	auto ui = make_empty_ui_reporter();
	ui.on_scan_complete = [&scanning_done] { scanning_done = true; };
	while (!scanning_done) {
		REQUIRE_NOTHROW(scuff::ui_update(ui));
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	auto grp_ui = make_empty_group_reporter();
	grp_ui.on_device_late_create = [&device_late_created](scuff::create_device_result result) {
		device_late_created = true;
	};
	while (!device_late_created) {
		REQUIRE_NOTHROW(scuff::ui_update(group_id, grp_ui));
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	REQUIRE(scuff::was_created_successfully(device.id));
	CHECK_NOTHROW(scuff::erase(device.id));
	CHECK_NOTHROW(scuff::erase(sbox_id));
	CHECK_NOTHROW(scuff::erase(group_id));
}

//TEST_CASE("finish scanning") {
//	bool done = false;
//	auto ui = make_empty_ui_reporter();
//	ui.on_scan_complete = [&done] { done = true; };
//	while (!done) {
//		scuff::ui_update(ui);
//		std::this_thread::sleep_for(std::chrono::milliseconds(100));
//	}
//}

auto create_a_bunch_of_devices(scuff::id::sandbox sbox) -> std::vector<scuff::id::device> {
	auto devices = std::vector<scuff::id::device>{};
	const auto plugins = scuff::get_working_plugins();
	size_t i = 0;
	for (const auto plugin : plugins) {
		const auto type = scuff::get_type(plugin);
		const auto ext_id = scuff::get_ext_id(plugin);
		const auto pf     = scuff::get_plugfile(plugin);
		const auto path   = scuff::get_path(pf);
		INFO("creating device: ", path, " id: ", ext_id.value, " index: ", i);
		const auto device = scuff::create_device(sbox, type, ext_id);
		REQUIRE(device.success);
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
		REQUIRE(device.success);
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
	REQUIRE(device.success);
}

TEST_CASE("lifetimes") {
	scuff::create_device_result device1, device2, device3, device4;
	scuff::id::group group1;
	scuff::id::sandbox sbox1, sbox2;
	CHECK_NOTHROW(group1 = scuff::create_group(nullptr));
	CHECK_NOTHROW(sbox1  = scuff::create_sandbox(group1, sbox_exe_path_.string()));
	CHECK_NOTHROW(scuff::activate(group1, 44100.0));
	CHECK_NOTHROW(scuff::erase(group1));
	CHECK_NOTHROW(sbox2 = scuff::create_sandbox(group1, sbox_exe_path_.string()));
	CHECK_NOTHROW(device1 = scuff::create_device(sbox1, scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"}));
	REQUIRE      (device1.success);
	CHECK_NOTHROW(scuff::erase(sbox1));
	CHECK_NOTHROW(device2 = scuff::create_device(sbox1, scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"}));
	REQUIRE      (device2.success);
	CHECK_NOTHROW(scuff::erase(device1.id));
	CHECK_NOTHROW(scuff::erase(device2.id));
	CHECK_THROWS (device3 = scuff::create_device(sbox1, scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"}));
	CHECK_NOTHROW(scuff::erase(sbox2));
	CHECK_THROWS (scuff::erase(device3.id));
	CHECK_THROWS (device4 = scuff::create_device(sbox1, scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"}));
	CHECK_THROWS (sbox1 = scuff::create_sandbox(group1, sbox_exe_path_.string()));
}

TEST_CASE("single-sandbox rack connections") {
	scuff::create_device_result device1, device2, device3, device4;
	scuff::id::group group1;
	scuff::id::sandbox sbox1;
	CHECK_NOTHROW(group1 = scuff::create_group(nullptr));
	CHECK_NOTHROW(sbox1  = scuff::create_sandbox(group1, sbox_exe_path_.string()));
	CHECK_NOTHROW(scuff::activate(group1, 44100.0));
	CHECK_NOTHROW(device1 = scuff::create_device(sbox1, scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"}));
	CHECK_NOTHROW(device2 = scuff::create_device(sbox1, scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"}));
	CHECK_NOTHROW(device3 = scuff::create_device(sbox1, scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"}));
	CHECK_NOTHROW(device4 = scuff::create_device(sbox1, scuff::plugin_type::clap, {"studio.kx.distrho.MaGigaverb"}));
	REQUIRE      (device1.success);
	REQUIRE      (device2.success);
	REQUIRE      (device3.success);
	REQUIRE      (device4.success);
	CHECK_NOTHROW(scuff::connect(device1.id, 0, device2.id, 0));
	CHECK_NOTHROW(scuff::connect(device2.id, 0, device3.id, 0));
	CHECK_NOTHROW(scuff::connect(device3.id, 0, device4.id, 0));
	scuff::group_process gp;
	scuff::audio_input in;
	scuff::audio_output out;
	in.dev_id      = device1.id;
	in.port_index  = 0;
	in.write_to    = [](float* floats) { for (int i = 0; i < scuff::VECTOR_SIZE * scuff::CHANNEL_COUNT; i++) { floats[i] = 0.0f; } };
	out.dev_id     = device4.id;
	out.port_index = 0;
	out.read_from  = [](const float* floats) {};
	gp.group = group1;
	gp.audio_inputs.push_back(in);
	gp.audio_outputs.push_back(out);
	gp.input_events.count = [] { return 0; };
	gp.input_events.pop   = [](size_t, scuff::input_event*) { return 0; };
	gp.output_events.push = [](const scuff::output_event&) {};
	CHECK_NOTHROW(scuff::audio_process(gp));
	// Move the last device to the front ...
	CHECK_NOTHROW(scuff::disconnect(device3.id, 0, device4.id, 0));
	// Keep processing audio while the devices are being rewired ...
	CHECK_NOTHROW(scuff::audio_process(gp));
	CHECK_NOTHROW(scuff::connect(device4.id, 0, device1.id, 0));
	CHECK_NOTHROW(scuff::audio_process(gp));
	gp.audio_inputs[0].dev_id = device4.id;
	gp.audio_outputs[0].dev_id = device3.id;
	CHECK_NOTHROW(scuff::audio_process(gp));
	CHECK_NOTHROW(scuff::erase(device1.id));
	CHECK_NOTHROW(scuff::erase(device2.id));
	CHECK_NOTHROW(scuff::erase(device3.id));
	CHECK_NOTHROW(scuff::erase(device4.id));
	CHECK_NOTHROW(scuff::erase(sbox1));
	CHECK_NOTHROW(scuff::erase(group1));

}

//TEST_CASE("stress test") {
//	auto group = scuff::managed_group{scuff::create_group(nullptr)};
//	const auto sbox    = scuff::managed_sandbox{scuff::create_sandbox(group.id(), sbox_exe_path_.string())};
//	scuff::activate(group.id(), 44100.0);
//	SUBCASE ("erase the group") {
//		group = {};
//	}
//	SUBCASE ("deactivate group") {
//		scuff::deactivate(group.id());
//	}
//	auto devices = create_a_bunch_of_devices(sbox.id());
//	SUBCASE ("just erase the group") {
//		group = {};
//	}
//	SUBCASE ("erase devices") {
//		SUBCASE ("deactivate group first") {
//			scuff::deactivate(group.id());
//		}
//		for (const auto dev : devices) {
//			scuff::erase(dev);
//		}
//	}
//}
