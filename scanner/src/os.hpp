#pragma once

#include <clap/entry.h>
#include <filesystem>
#include <vector>

namespace scanner {
namespace os {

[[nodiscard]] auto could_be_a_vst2_file(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto find_clap_entry(const std::filesystem::path& path) -> const clap_plugin_entry_t*;
[[nodiscard]] auto get_env_search_paths(char path_delimiter) -> std::vector<std::filesystem::path>;
[[nodiscard]] auto get_system_search_paths() -> std::vector<std::filesystem::path>;
[[nodiscard]] auto is_clap_file(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto is_vst3_file(const std::filesystem::path& path) -> bool;

} // os
} // scanner