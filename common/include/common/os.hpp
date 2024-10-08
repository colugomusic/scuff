#pragma once

#include <clap/entry.h>
#include <clap/ext/gui.h>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

namespace scuff::os {

[[nodiscard]] auto could_be_a_vst2_file(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto find_clap_entry(const std::filesystem::path& path) -> const clap_plugin_entry_t*;
[[nodiscard]] auto get_env_search_paths(char path_delimiter) -> std::vector<std::filesystem::path>;
[[nodiscard]] auto get_system_search_paths() -> std::vector<std::filesystem::path>;
[[nodiscard]] auto is_clap_file(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto is_vst3_file(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto redirect_stream(FILE* stream) -> int;
auto restore_stream(FILE* stream, int old) -> void;
auto set_realtime_priority(std::jthread* thread) -> void;

} // scuff::os

#if defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <boost/process.hpp>
#include <boost/process/v1/windows.hpp>

namespace bp = boost::process;

namespace scuff::os {

[[nodiscard]] static
auto get_process_id() -> int {
	return int(GetCurrentProcessId());
}

[[nodiscard]] static
auto get_clap_window_api() -> const char* {
	return CLAP_WINDOW_API_WIN32;
}

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::string& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, bp::windows::hide, std::forward<BoostArgs>(boost_args)...};
}

} // scuff::os

#elif defined(__APPLE__)

#include <unistd.h>
#include <boost/process.hpp>

namespace bp = boost::process;

namespace scuff::os {

[[nodiscard]] static
auto get_process_id() -> int {
	return getpid();
}

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::string& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, std::forward<BoostArgs>(boost_args)...};
}

[[nodiscard]] static
auto get_clap_window_api() -> const char* {
	return CLAP_WINDOW_API_COCOA;
}

} // scuff::os

#elif defined(__linux__)

#include <unistd.h>
#include <boost/process.hpp>

namespace bp = boost::process;

namespace scuff::os {

[[nodiscard]] static
auto get_process_id() -> int {
	return getpid();
}

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::string& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, std::forward<BoostArgs>(boost_args)...};
}

[[nodiscard]] static
auto get_clap_window_api() -> const char* {
	return CLAP_WINDOW_API_X11;
}

} // scuff::os

#endif
