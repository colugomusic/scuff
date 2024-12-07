#pragma once

#include "common-types.hpp"
#include <stdint.h>

namespace scuff {

// This should be a superset of CLAP and VST3 parameter functionality.
// I haven't started VST3 implementation yet and AFAIK VST3 doesn't have
// parameter flags so this is basically just the CLAP parameter flags at
// the moment. So look at the clap headers for documentation. When we get
// to implementing VST3 this will probably change to better accommodate
// both formats.
enum param_flags {
	param_is_stepped                 = 1 << 0,
	param_is_periodic                = 1 << 1,
	param_is_hidden                  = 1 << 2,
	param_is_readonly                = 1 << 3,
	param_is_bypass                  = 1 << 4,
	param_is_automatable             = 1 << 5,
	param_is_automatable_per_note_id = 1 << 6,
	param_is_automatable_per_key     = 1 << 7,
	param_is_automatable_per_channel = 1 << 8,
	param_is_automatable_per_port    = 1 << 9,
	param_is_modulatable             = 1 << 10,
	param_is_modulatable_per_note_id = 1 << 11,
	param_is_modulatable_per_key     = 1 << 12,
	param_is_modulatable_per_channel = 1 << 13,
	param_is_modulatable_per_port    = 1 << 14,
	param_requires_process           = 1 << 15,
	param_is_enum                    = 1 << 16,
};

struct param_info {
	ext::id::param id; // Either a clap_id or a Steinberg::Vst::ParamID
	uint32_t flags;
	std::string name;
	double min_value;
	double max_value;
	double default_value;
};

struct client_param_info : param_info {};

struct sbox_param_info : param_info {
	struct {
		void* cookie;
	} clap;
};

} // scuff
