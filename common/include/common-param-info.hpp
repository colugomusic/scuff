#pragma once

#include "common-types.hpp"
#include <stdint.h>

namespace scuff {

struct common_param_info {
	ext::id::param id; // Either a clap_id or a Steinberg::Vst::ParamID
	uint32_t flags;    // TOODOO: parameter flags
	std::string name;
	double min_value;
	double max_value;
	double default_value;
};

struct client_param_info : common_param_info {};

struct sbox_param_info : common_param_info {
	struct {
		void* cookie;
	} clap;
};

} // scuff
