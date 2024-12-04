#pragma once

#include "common-constants.hpp"
#include "common-types.hpp"
#include <stdint.h>

namespace scuff {

// FIXME: There's no need to store all this in shared memory.
//        And this is a lot since we have to reserve space for
//        MAX_PARAMS * sizeof(param_info) per device!
//        This data can just be looked up, or replicated to
//        local storage in the sandbox and client every time
//        the parameters are re-scanned.
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
