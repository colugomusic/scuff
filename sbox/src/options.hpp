#pragma once

#include "common/types.hpp"
#include <string>

namespace scuff::sbox {

struct options {
	std::string group_shmid;
	std::string sbox_shmid;
	double sample_rate = 0.0;
};

} // scuff::sbox
