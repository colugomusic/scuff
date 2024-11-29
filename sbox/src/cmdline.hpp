#pragma once

#include "options.hpp"
#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace scuff::sbox::cmdline {

auto get_options(int argc, const char* argv[]) -> sbox::options {
	sbox::options options;
	try {
		po::options_description desc("Allowed options");
		desc.add_options()
			("group",   po::value<std::string>(&options.group_shmid),"Group shared memory ID")
			("sandbox", po::value<std::string>(&options.sbox_shmid), "Sandbox shared memory ID")
			;
		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	}
	catch (...) {}
	return options;
}

} // scuff::sbox::cmdline
