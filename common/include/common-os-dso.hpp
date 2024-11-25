#pragma once

#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

namespace scuff::os::dso {

struct path    { std::filesystem::path value; auto operator<=>(const path&) const = default; };
struct fn_name { std::string value; auto operator<=>(const fn_name&) const = default; };

[[nodiscard]] auto find_fn(void* lib, const dso::fn_name& fn_name) -> void*;
[[nodiscard]] auto open_lib(const dso::path& path) -> void*;
auto release_lib(void* lib) -> void;

struct entry {
	dso::path path;
	dso::fn_name fn_name;
	void* lib    = nullptr;
	void* fn_ptr = nullptr;
	~entry() { if (lib) { release_lib(lib); } }
};

struct model {
	std::vector<entry> entries;
};

static model M_;

template <typename FnT>
auto find_fn(const dso::path& path, const dso::fn_name& fn_name) -> const FnT* {
	const auto match = [&path, &fn_name](const dso::entry& entry) { return entry.path == path && entry.fn_name == fn_name; };
	if (const auto pos = std::ranges::find_if(M_.entries, match); pos != M_.entries.end()) {
		return reinterpret_cast<FnT*>(pos->fn_ptr);
	}
	if (const auto lib = open_lib(path)) {
		dso::entry entry;
		entry.path    = path;
		entry.fn_name = fn_name;
		entry.lib     = lib;
		entry.fn_ptr  = find_fn(lib, fn_name);
		if (!entry.fn_ptr) {
			return nullptr;
		}
		M_.entries.push_back(entry);
		return reinterpret_cast<FnT*>(entry.fn_ptr);
	}
	return nullptr;
}

} // scuff::os::dso
