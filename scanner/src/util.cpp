#include "util.hpp"
#include <flux.hpp>

namespace scanner {
namespace util {

auto to_upper(std::string_view s) -> std::string {
	const auto fn = [](char c) { return std::toupper(c); };
	return flux::ref(s).map(fn).to<std::string>();
}

auto has_extension_case_insensitive(const std::filesystem::path& path, std::string_view ext) -> bool {
	return to_upper(path.extension().string()) == to_upper(ext);
}

} // util
} // scanner
