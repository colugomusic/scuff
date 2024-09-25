#pragma once

#include "constants.hpp"
#include "types.hpp"
#include <stdint.h>

namespace scuff {

struct param_info {
	ext::id::param id; // Either a clap_id or a Steinberg::Vst::ParamID
	uint32_t flags;    // TODO: parameter flags
	char name[scuff::PARAM_NAME_MAX];
	double min_value;
	double max_value;
	double default_value;
	struct {
		void* cookie;
	} clap;
};

} // scuff
