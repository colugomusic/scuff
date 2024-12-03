#pragma once

#include "common-types.hpp"
#include <edwin.hpp>
#include <string>

namespace scuff::sbox {

struct options {
	std::string group_shmid;
	std::string sbox_shmid;
	std::string plugfile_gui;
	bool test = false;
	edwin::native_handle parent_window;
};

} // scuff::sbox
