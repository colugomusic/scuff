#include "os.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
#include <unistd.h>

namespace scanner {
namespace os {

auto could_be_a_vst2_file(const std::filesystem::path& path) -> bool {
	return path.extension() == ".dylib";
}

auto find_clap_entry(const std::filesystem::path& path) -> const clap_plugin_entry_t* {
	auto ps = path.u8string();
	auto cs = CFStringCreateWithBytes(kCFAllocatorDefault, (uint8_t *)ps.c_str(), ps.size(), kCFStringEncodingUTF8, false);
	auto bundleURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cs, kCFURLPOSIXPathStyle, true);
	auto bundle = CFBundleCreate(kCFAllocatorDefault, bundleURL); 
	auto db = CFBundleGetDataPointerForName(bundle, CFSTR("clap_entry")); 
	CFRelease(bundle);
	CFRelease(bundleURL);
	CFRelease(cs); 
	return (clap_plugin_entry_t *)db;
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

} // os
} // scanner
