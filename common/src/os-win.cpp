#include "common/c_constants.h"
#include "common/os.hpp"
#include "common/util.hpp"
#include <flux.hpp>
#include <io.h>
#include <optional>
#include <ShlObj.h>

namespace scuff {
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
		return reinterpret_cast<clap_plugin_entry_t*>(GetProcAddress(lib, SCUFF_CLAP_SYMBOL_ENTRY));
	}
	return nullptr;
}

auto get_env_search_paths(char path_delimiter) -> std::vector<std::filesystem::path> {
	char* p = nullptr;
	size_t len = 0;
	if (_dupenv_s(&p, &len, "CLAP_PATH") == 0 && p != nullptr) {
		auto paths = flux::from(std::string(p))
			.split_string(path_delimiter)
			.to<std::vector<std::filesystem::path>>();
		free(p);
		return paths;
	}
	return {};
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
		scuff::util::has_extension_case_insensitive(path, SCUFF_CLAP_EXT);
}

auto is_vst3_file(const std::filesystem::path& path) -> bool {
	return scuff::util::has_extension_case_insensitive(path, SCUFF_VST3_EXT);
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

auto set_realtime_priority(std::jthread* thread) -> void {
	const auto handle = thread->native_handle();
	SetPriorityClass(handle, REALTIME_PRIORITY_CLASS);
	SetThreadPriority(handle, THREAD_PRIORITY_TIME_CRITICAL);
}

} // os
} // scuff
