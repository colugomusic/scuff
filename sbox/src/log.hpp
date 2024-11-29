#pragma once

#include "data.hpp"
#include <string_view>

namespace scuff::sbox {

template <typename... Args> static
auto log(sbox::app* app, std::string_view fmt, Args&&... args) -> void {
	log_printf(fmt.data(), std::forward<Args>(args)...);
	debug_ui::log(&app->debug_ui, fmt, std::forward<Args>(args)...);
}

template <typename... Args> static
auto debug_log(const char* fmt, Args&&... args) -> void {
#if _DEBUG
	log_printf(fmt, std::forward<Args>(args)...);
#endif
}

} // scuff::sbox