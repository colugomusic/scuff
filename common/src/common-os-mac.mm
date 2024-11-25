#include "common-os.hpp"
#include "common-os-dso.hpp"
#include <flux.hpp>
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

namespace scuff::os::dso {

auto find_fn(void* lib, const dso::fn_name& fn_name) -> void* {
	auto cs = CFStringCreateWithBytes(kCFAllocatorDefault, (uint8_t*)fn_name.value.c_str(), fn_name.value.size(), kCFStringEncodingUTF8, false);
	auto db = CFBundleGetDataPointerForName((CFBundleRef)(lib), cs);
	CFRelease(cs);
	return db;
}

auto open_lib(const dso::path& path) -> void* {
	auto ps = path.value.u8string();
	auto cs = CFStringCreateWithBytes(kCFAllocatorDefault, (uint8_t*)ps.c_str(), ps.size(), kCFStringEncodingUTF8, false);
	auto bundleURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cs, kCFURLPOSIXPathStyle, true);
	auto bundle = CFBundleCreate(kCFAllocatorDefault, bundleURL);
	CFRelease(bundleURL);
	CFRelease(cs); 
	return bundle;

}

auto release_lib(void* lib) -> void {
	auto bundle = reinterpret_cast<CFTypeRef>(lib);
	CFRelease(bundle);
}

} // scuff::os::dso

namespace scuff::os {

auto could_be_a_vst2_file(const std::filesystem::path& path) -> bool {
	return path.extension() == ".dylib";
}

auto get_process_id() -> int {
	return getpid();
}

auto get_clap_window_api() -> const char* {
	return CLAP_WINDOW_API_COCOA;
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
	auto *fileManager = [NSFileManager defaultManager];
	auto *userLibURLs = [fileManager URLsForDirectory:NSLibraryDirectory inDomains:NSUserDomainMask];
	auto *sysLibURLs = [fileManager URLsForDirectory:NSLibraryDirectory inDomains:NSLocalDomainMask];
	if (userLibURLs) {
		auto *u = [userLibURLs objectAtIndex:0];
		auto p = std::filesystem::path{[u fileSystemRepresentation]} / "Audio" / "Plug-Ins" / "CLAP";
		paths.push_back(p);
	}
	if (sysLibURLs) {
		auto *u = [sysLibURLs objectAtIndex:0];
		auto p = std::filesystem::path{[u fileSystemRepresentation]} / "Audio" / "Plug-Ins" / "CLAP";
		paths.push_back(p);
	}
	auto env_paths = get_env_search_paths(':');
	paths.insert(paths.end(), env_paths.begin(), env_paths.end());
	return paths;
}

auto is_clap_file(const std::filesystem::path& path) -> bool {
	return
		std::filesystem::is_directory(path) &&
		util::has_extension_case_insensitive(path, CLAP_EXT);
}

auto is_vst3_file(const std::filesystem::path& path) -> bool {
	return util::has_extension_case_insensitive(path, VST3_EXT);
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
