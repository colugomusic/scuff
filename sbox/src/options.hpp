#pragma once

#include "common/types.hpp"
#include <string>

namespace scuff::sbox {

struct options {
	std::string instance_id;
	id::group group_id;
	id::sandbox sbox_id;
	double sample_rate = 0.0;
};

} // scuff::sbox
