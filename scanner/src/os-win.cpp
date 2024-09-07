#include "constants.hpp"
#include "os.hpp"
#include "util.hpp"
#include <flux.hpp>
#include <io.h>
#include <optional>
#include <ShlObj.h>

namespace scanner {
namespace os {

[[nodiscard]] static
auto get_known_folder(KNOWNFOLDERID id) -> std::optional<std::filesystem::path> {
	PWSTR path;
	if (SHGetKnownFolderPath(id, 0, nullptr, &path) == S_OK) {
		std::filesystem::path p{path};
		CoTaskMemFree(path);
		return p;
	}
	CoTaskMemFree(path);
	return std::nullopt;
}

auto could_be_a_vst2_file(const std::filesystem::path& path) -> bool {
	return path.extension() == ".dll";
}

auto find_clap_entry(const std::filesystem::path& path) -> const clap_plugin_entry_t* {
	if (auto lib = LoadLibrary((LPCSTR)(path.generic_string().c_str()))) {
		return reinterpret_cast<clap_plugin_entry_t*>(GetProcAddress(lib, "clap_entry"));
	}
	return nullptr;
}

auto get_system_search_paths() -> std::vector<std::filesystem::path> {
	std::vector<std::filesystem::path> paths;
	if (auto p = get_known_folder(FOLDERID_ProgramFilesCommon)) {
		paths.push_back(*p / "CLAP");
	}
	if (auto p = get_known_folder(FOLDERID_UserProgramFiles)) {
		paths.push_back(*p / "Common" / "CLAP");
	}
	auto env_paths = get_env_search_paths(';');
	paths.insert(paths.end(), env_paths.begin(), env_paths.end());
	return paths;
}

auto is_clap_file(const std::filesystem::path& path) -> bool {
	return
		!std::filesystem::is_directory(path) &&
		util::has_extension_case_insensitive(path, CLAP_EXT);
}

auto is_vst3_file(const std::filesystem::path& path) -> bool {
	return util::has_extension_case_insensitive(path, VST3_EXT);
}

auto redirect_stream(FILE* stream) -> int {
	fflush(stream);
	// Save the current stdout
	const auto old = _dup(_fileno(stream));
	// Redirect stdout to NUL on Windows
	FILE* null_file = nullptr;
	freopen_s(&null_file, "NUL", "w", stream);
	return old;
}

auto restore_stream(FILE* stream, int old) -> void {
	if (old != -1) {
		fflush(stream);
		// Restore the original stdout on Windows
		_dup2(old, _fileno(stream));
		_close(old);
	}
}

} // os
} // scanner
