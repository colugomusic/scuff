#pragma once

#include <filesystem>
#include <vector>

namespace scanner {
namespace util {

[[nodiscard]] auto to_upper(std::string_view s) -> std::string;
[[nodiscard]] auto has_extension_case_insensitive(const std::filesystem::path& path, std::string_view ext) -> bool;

} // util
} // scanner
