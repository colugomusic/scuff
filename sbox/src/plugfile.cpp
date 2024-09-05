#include "plugfile.hpp"
#include "dll.hpp"
#include <clap/entry.h>
#include <clap/factory/plugin-factory.h>
#include <format>
#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sbox::plugfile {

static constexpr auto CLAP_FILE_EXTENSION = ".clap";
static constexpr auto VST3_FILE_EXTENSION = ".vst3";
static constexpr auto CLAP_SYMBOL_ENTRY   = "clap_entry";

struct clap_iface_entry   { const clap_plugin_entry_t* value = nullptr; };
struct clap_iface_factory { const clap_plugin_factory_t* value = nullptr; };
struct error { std::string value; };

struct clap_file {
	clap_iface_entry   iface_entry;
	clap_iface_factory iface_factory;
};

struct vst2_file {
};

struct vst3_file {
};

using type_file = std::variant<
	clap_file,
	vst2_file,
	vst3_file
>;

struct file {
	boost::dll::shared_library dll;
	plugfile::error error;
	fs::path path;
	plugfile::type_file type_file;
};

struct model {
	std::vector<file> files;
	std::unordered_map<fs::path, tom::id::plugfile> lookup;
};

static std::unique_ptr<model> M_;

[[nodiscard]] static
auto get_files_in_dir(fs::path path) -> std::vector<fs::path> {
	std::vector<fs::path> files;
	if (!fs::exists(path)) { return files; }
	for (const auto& entry : fs::directory_iterator{path}) {
		if (entry.is_regular_file()) {
			files.push_back(entry.path());
		}
	}
	return files;
}

[[nodiscard]] static
auto load_dll(fs::path path) -> std::optional<boost::dll::shared_library> {
	boost::system::error_code error;
	auto lib = boost::dll::shared_library{path.string(), error};
	if (error.value() == boost::system::errc::success) {
		return boost::dll::shared_library{lib};
	}
#	if defined(__APPLE__)
		for (const auto& possible_dylib : get_files_in_dir(path / "Contents" / "MacOS")) {
			auto lib = boost::dll::shared_library{possible_dylib.string(), error};
			if (error.value() == boost::system::errc::success) {
				return boost::dll::shared_library{lib};
			}
		}
#	endif
	return std::nullopt;
}

static
auto set_error(tom::id::plugfile idx, std::string error) -> void {
	M_->files[idx.value].error = plugfile::error{std::move(error)};
}

[[nodiscard]] static
auto lowercase(const std::string& str) -> std::string {
	std::string result;
	result.reserve(str.size());
	for (const auto c : str) {
		result.push_back(std::tolower(c));
	}
	return result;
}

[[nodiscard]] static
auto does_extension_match(const fs::path& path, const std::string& ext) -> bool {
	return lowercase(path.extension().string()) == ext;
}

[[nodiscard]] static
auto looks_like_a_clap_file(const fs::path& path) -> bool {
	return does_extension_match(path, CLAP_FILE_EXTENSION);
}

[[nodiscard]] static
auto looks_like_a_vst3_file(const fs::path& path) -> bool {
	return does_extension_match(path, VST3_FILE_EXTENSION);
}

[[nodiscard]] static
auto could_maybe_be_a_vst2_file(const fs::path& path) -> bool {
	const auto dso_suffix = boost::dll::shared_library::suffix();
	return does_extension_match(path, dso_suffix.string());
}

static
auto try_load_clap(tom::id::plugfile idx) -> void {
	const auto& path = get_path(idx);
	const auto dll   = load_dll(path);
	if (!dll) {
		set_error(idx, std::format("Failed to open shared library: {}", path.string()));
		return;
	}
	if (!dll->has(CLAP_SYMBOL_ENTRY)) {
		set_error(idx, std::format("Unable to resolve symbol '{}' in shared library: {}", CLAP_SYMBOL_ENTRY, path.string()));
		return;
	}
	const auto& entry = dll->get<const clap_plugin_entry_t>(CLAP_SYMBOL_ENTRY);
	if (!entry.init(path.string().c_str())) {
		set_error(idx, std::format("init() failed for plugin: {}", path.string()));
		return;
	}
	const auto factory = static_cast<const clap_plugin_factory_t*>(entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
	plugfile::clap_file clap_file;
	clap_file.iface_entry   = clap_iface_entry{&entry};
	clap_file.iface_factory = clap_iface_factory{factory};
	M_->files[idx.value].dll       = *dll;
	M_->files[idx.value].type_file = clap_file;
}

static
auto try_load_vst2(tom::id::plugfile idx) -> void {
	set_error(idx, "VST2 support is not implemented yet.");
}

static
auto try_load_vst3(tom::id::plugfile idx) -> void {
	set_error(idx, "VST3 support is not implemented yet.");
}

static
auto try_load(tom::id::plugfile idx) -> void {
	const auto& path = get_path(idx);
	if (looks_like_a_clap_file(path)) {
		try_load_clap(idx);
		return;
	}
	if (looks_like_a_vst3_file(path)) {
		try_load_vst3(idx);
		return;
	}
	if (could_maybe_be_a_vst2_file(path)) {
		try_load_vst2(idx);
		return;
	}
	set_error(idx, std::format("I'm not sure what kind of plugin file this is: {}", path.string()));
}

auto create() -> void {
	M_ = std::make_unique<model>();
}

auto destroy() -> void {
	M_.reset();
}

[[nodiscard]] static
auto add(const fs::path& path) -> tom::id::plugfile {
	// TODO:
	return {};
	//auto idx = tom::plugfile{M_->files.size()};
	//M_->files.resize(idx.value + 1);
	//M_->files[idx.value].path = path;
	//try_load(idx);
	//return idx;
}

[[nodiscard]]
auto get_path(tom::id::plugfile idx) -> const fs::path& {
	// TODO:
	return {};//M_->files[idx.value].path;
}

[[nodiscard]]
auto find(const fs::path& path) -> tom::id::plugfile {
	if (const auto pos = M_->lookup.find(path); pos != M_->lookup.end()) {
		return pos->second;
	}
	return {};
}

} // sbox::plugfile
