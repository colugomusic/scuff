#pragma once

#include <string_view>

namespace scuff {

enum class plugin_type { unknown, clap, vst3 };

[[nodiscard]] static
auto to_string(scuff::plugin_type type) -> std::string {
	switch (type) {
		case scuff::plugin_type::clap: { return "clap"; }
		case scuff::plugin_type::vst3: { return "vst3"; }
		default:                       { return "unknown"; }
	}
}

[[nodiscard]] static
auto plugin_type_from_string(std::string_view str) -> plugin_type {
	if (str == "clap") { return scuff::plugin_type::clap; }
	if (str == "vst3") { return scuff::plugin_type::vst3; }
	return scuff::plugin_type::unknown;
}

} // scuff
