#include "common-constants.hpp"
#include "common-os.hpp"
#include "common-os-dso.hpp"
#include "common-util.hpp"
#include <dlfcn.h>
#include <flux.hpp>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

namespace scuff::os::dso {

auto find_fn(void* lib, const dso::fn_name& fn_name) -> void* {
	return dlsym(lib, fn_name.value.c_str());
}

auto open_lib(const dso::path& path) -> void* {
	return dlopen(path.value.c_str(), RTLD_LOCAL | RTLD_LAZY);
}

auto release_lib(void* lib) -> void {
	dlclose(lib);
}

} // scuff::os::dso

namespace scuff::os {

auto could_be_a_vst2_file(const std::filesystem::path& path) -> bool {
	return path.extension() == ".so";
}

auto get_process_id() -> int {
	return getpid();
}

auto get_clap_window_api() -> const char* {
	return CLAP_WINDOW_API_X11;
}

auto get_env_search_paths(char path_delimiter) -> std::vector<std::filesystem::path> {
	if (auto p = getenv("CLAP_PATH")) {
		return flux::from(std::string(p))
			.split_string(path_delimiter)
			.to<std::vector<std::filesystem::path>>();
	}
	return {};
}

auto get_system_search_paths() -> std::vector<std::filesystem::path> {
	std::vector<std::filesystem::path> paths;
	paths.push_back("/usr/lib/clap");
	paths.push_back(std::filesystem::path(getenv("HOME")) / std::filesystem::path(".clap"));
	auto env_paths = get_env_search_paths(':');
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

auto process_is_running(int pid) -> bool {
	if (kill(pid, 0) == 0) {
		return true; // Process is running
	}
	else {
		if (errno == ESRCH) {
			// ESRCH: No such process
			return false;
		}
		else if (errno == EPERM) {
			// EPERM: Process exists but no permission to signal it
			return true;
		}
	}
	return false;
}

auto redirect_stream(FILE* stream) -> int {
	fflush(stream);
	// Save the current stdout
	const auto old = dup(fileno(stream));
	// Redirect stdout to /dev/null on Unix-like systems
	FILE* null_file = freopen("/dev/null", "w", stream);
	return old;
}

auto restore_stream(FILE* stream, int old) -> void {
	if (old != -1) {
		fflush(stream);
		// Restore the original stdout on Unix-like systems
		dup2(old, fileno(stream));
		close(old);
	}
}

auto set_realtime_priority(std::jthread* thread) -> void {
	const auto handle = thread->native_handle();
	struct sched_param param;
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	pthread_setschedparam(handle, SCHED_FIFO, &param);
}

} // scuff::os
