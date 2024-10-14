#pragma once

#include "common-constants.hpp"
#include "common-types.hpp"
#include <stdint.h>

namespace scuff {

struct param_info {
	ext::id::param id; // Either a clap_id or a Steinberg::Vst::ParamID
	uint32_t flags;    // TOODOO: parameter flags
	char name[scuff::PARAM_NAME_MAX];
	double min_value;
	double max_value;
	double default_value;
	struct {
		void* cookie;
	} clap;
};

} // scuff
