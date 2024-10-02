#pragma once

#include "os.hpp"
#include <clap/ext/gui.h>
#include <clap/ext/params.h>

namespace scuff {

template <typename T> [[nodiscard]] static
auto get_plugin_ext(const clap_plugin& iface, const char* id, const char* fallback_id = nullptr) -> const T* {
	auto ptr = static_cast<const T*>(iface.get_extension(&iface, id));
	if (!ptr && fallback_id) {
		ptr = static_cast<const T*>(iface.get_extension(&iface, fallback_id));
	}
	return ptr;
}

[[nodiscard]] static
auto has_gui(const clap_plugin_t& iface) -> bool {
	const auto gui = get_plugin_ext<clap_plugin_gui_t>(iface, CLAP_EXT_GUI);
	if (!gui) {
		return false;
	}
	return gui->is_api_supported(&iface, scuff::os::get_clap_window_api(), false);
}

[[nodiscard]] static
auto has_params(const clap_plugin_t& iface) -> bool {
	const auto params = get_plugin_ext<clap_plugin_params_t>(iface, CLAP_EXT_PARAMS);
	if (!params) {
		return false;
	}
	return params->count(&iface) > 0;
}

} // scuff