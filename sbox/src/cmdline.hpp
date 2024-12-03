#pragma once

#include "data.hpp"
#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace scuff::sbox::cmdline {

auto get_options(const sbox::app& app, int argc, const char* argv[]) -> sbox::options {
	sbox::options options;
	po::options_description desc("Allowed options");
	uint64_t parent_window = 0;
	desc.add_options()
		("group",         po::value<std::string>(&options.group_shmid),"Group shared memory ID")
		("sandbox",       po::value<std::string>(&options.sbox_shmid), "Sandbox shared memory ID")
		("gui",           po::value<std::string>(&options.plugfile_gui), "Path to plugfile GUI to open for testing")
		("test", po::bool_switch(&options.test), "Run tests")
		("parent-window", po::value<uint64_t>(&parent_window), "Parent window handle")
		;
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);
	options.parent_window.value = reinterpret_cast<void*>(parent_window);
	return options;
}

} // scuff::sbox::cmdline
