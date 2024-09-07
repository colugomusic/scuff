#include "os.hpp"
#include <flux.hpp>

namespace scanner {
namespace os {

auto get_env_search_paths(char path_delimiter) -> std::vector<std::filesystem::path> {
	if (auto p = getenv("CLAP_PATH")) {
		return flux::from(std::string(p))
			.split_string(path_delimiter)
			.to<std::vector<std::filesystem::path>>();
	}
	return {};
}

} // os
} // scanner
