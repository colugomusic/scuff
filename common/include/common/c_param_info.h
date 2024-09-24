#pragma once

#include "c_constants.h"
#include <stdint.h>

typedef struct scuff_param_info_t {
	uint32_t id;    // Either a clap_id or a Steinberg::Vst::ParamID
	uint32_t flags; // TODO: parameter flags
	char name[SCUFF_PARAM_NAME_MAX];
	double min_value;
	double max_value;
	double default_value;
	struct {
		void* cookie;
	} clap;
} scuff_param_info;
