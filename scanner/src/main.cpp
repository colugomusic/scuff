#include <cxxopts.hpp>
#include <algorithm>
#include <ranges>
#include <vector>

namespace scanner {

struct options {
	std::vector<std::string> additional_search_paths;
};

[[nodiscard]] static
auto to_list(const std::string& str, const char delimiter = ';') -> std::vector<std::string> {
	auto split = std::views::split(delimiter);
	auto to_string = std::views::transform([](const auto& chars) {
		return std::string(std::begin(chars), std::end(chars));
	});
	auto strings = str | split | to_string;
	return std::vector<std::string>(std::begin(strings), std::end(strings));
}

[[nodiscard]] static
auto parse_options(int argc, const char* argv[]) -> options {
	auto options_parser = cxxopts::Options("scuff-scanner", "Scans the system for installed CLAP/VST plugins");
	options_parser.add_options()
		("s,search-paths", "List of additional search paths, separated by ';'", cxxopts::value<std::string>())
		;
	auto result = options_parser.parse(argc, argv);
	auto options = scanner::options{};
	options.additional_search_paths = scanner::to_list(result["search-paths"].as<std::string>());
	return options;
}

} // scanner

auto main(int argc, const char* argv[]) -> int {
	try {
		auto options = scanner::parse_options(argc, argv);
		for (const auto& path : options.additional_search_paths) {
			std::printf("%s\n", path.c_str());
		}
	} catch (const std::exception& e) {
		std::fprintf(stderr, "Error: %s\n", e.what());
		return 1;
	}
	return 0;
}