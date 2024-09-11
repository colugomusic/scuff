#pragma once

#include <filesystem>
#include <flux.hpp>
#include <vector>

namespace scanner {
namespace util {

[[nodiscard]] static
auto to_upper(std::string_view s) -> std::string {
	const auto fn = [](char c) { return std::toupper(c); };
	return flux::ref(s).map(fn).to<std::string>();
}

[[nodiscard]] static
auto has_extension_case_insensitive(const std::filesystem::path& path, std::string_view ext) -> bool {
	return to_upper(path.extension().string()) == to_upper(ext);
}

} // util
} // scanner
