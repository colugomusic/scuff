#pragma once

namespace scuff {

struct device_flags {
	enum e {
		has_gui    = 1 << 0,
		has_params = 1 << 1,
	};
	int value = 0;
};

struct device_port_info {
	size_t audio_input_port_count  = 0;
	size_t audio_output_port_count = 0;
};

} // scuff
