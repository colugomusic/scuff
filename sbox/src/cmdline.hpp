#pragma once

#include "options.hpp"
#include <nappgui.h>

namespace scuff::sbox::cmdline {

static constexpr auto ARGV_BUFFER_SIZE = 256;

[[nodiscard]] static
auto get_arg(uint32_t argv_index) -> std::string {
	char arg_buffer[ARGV_BUFFER_SIZE];
	osapp_argv(argv_index, arg_buffer, ARGV_BUFFER_SIZE);
	return arg_buffer;
}

[[nodiscard]] static
auto get_option(std::string_view key, uint32_t* arg_key_index, char* value) -> bool {
	const auto argc = osapp_argc();
	const auto arg  = get_arg(*arg_key_index);
	if (arg != key) {
		return false;
	}
	(*arg_key_index)++;
	if (*arg_key_index >= argc) {
		log_printf("Missing argument for %s", key.data());
		osapp_finish();
		return false;
	}
	osapp_argv(*arg_key_index, value, ARGV_BUFFER_SIZE);
	return true;
}

auto get_options() -> sbox::options {
	sbox::options options;
	const auto argc = osapp_argc();
	for (uint32_t i = 0; i < argc; i++) {
		char value[ARGV_BUFFER_SIZE];
		if (get_option("--instance-id", &i, value)) {
			options.instance_id = value;
			continue;
		}
		if (get_option("--group", &i, value)) {
			options.group_id.value = std::stoi(value);
			continue;
		}
		if (get_option("--sandbox", &i, value)) {
			options.sbox_id.value = std::stoi(value);
			continue;
		}
		if (get_option("--sr", &i, value)) {
			options.sample_rate = std::stod(value);
			continue;
		}
	}
	return options;
}

} // scuff::sbox::cmdline
