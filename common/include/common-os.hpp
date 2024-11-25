#pragma once

#include <clap/entry.h>
#include <clap/ext/gui.h>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

namespace scuff::os {

[[nodiscard]] auto could_be_a_vst2_file(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto get_clap_window_api() -> const char*;
[[nodiscard]] auto get_env_search_paths(char path_delimiter) -> std::vector<std::filesystem::path>;
[[nodiscard]] auto get_process_id() -> int;
[[nodiscard]] auto get_system_search_paths() -> std::vector<std::filesystem::path>;
[[nodiscard]] auto is_clap_file(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto is_vst3_file(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto redirect_stream(FILE* stream) -> int;
auto restore_stream(FILE* stream, int old) -> void;
auto set_realtime_priority(std::jthread* thread) -> void;

} // scuff::os
