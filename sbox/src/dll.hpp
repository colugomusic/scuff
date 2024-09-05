#pragma once

#include <boost/dll.hpp>
#include <boost/system/error_code.hpp>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace dll {

[[nodiscard]] inline
auto make_search_paths(const std::filesystem::path& file_path) {
	std::vector<std::filesystem::path> out; 
	const auto program_path = boost::dll::program_location().parent_path();
	const auto library_dir  = file_path.parent_path(); 
	out.push_back("");
	out.push_back(library_dir);
	out.push_back(program_path.string());
	out.push_back((program_path / (library_dir.string())).string());
#ifdef __APPLE__
	out.push_back((program_path / ".." / "Frameworks").string());
	out.push_back((program_path / ".." / "Frameworks" / (library_dir.string())).string());
#endif 
	return out;
}

[[nodiscard]] inline
auto load(std::filesystem::path path) -> boost::dll::shared_library {
	boost::system::error_code error; 
	auto dll = boost::dll::shared_library{path.string(), error};
	if (error.value() == boost::system::errc::success) {
		return dll;
	}
	const auto search_paths      = make_search_paths(path);
	const auto library_file_name = path.filename().string() + boost::dll::shared_library::suffix().string(); 
	for (const auto& search_path : search_paths) {
		dll = {(search_path / library_file_name).string(), error};
		if (error.value() == boost::system::errc::success) {
			return dll;
		}
	} 
	auto make_search_paths_list = [](std::vector<std::filesystem::path> paths){
		std::string out;
		for (const auto& path : paths) {
			out += path.string();
			out += "\n";
		} 
		return out;
	};
	const auto err = std::format(
		"Couldn't load library '{}'\n"
		"Tried these search paths and none of them worked:\n{}",
		library_file_name,
		make_search_paths_list(search_paths));
	throw std::runtime_error(err);
}

[[nodiscard]] inline
bool has(const boost::dll::shared_library& dll, std::string_view function_name) {
	return dll.has(function_name.data());
} 

template <typename F> [[nodiscard]]
auto get_fn(const boost::dll::shared_library& dll, std::string_view function_name) -> std::function<F> {
	if (!has(dll, function_name)) {
		const auto err = std::format(
			"No function '{}' in library: '{}'",
			function_name,
			dll.location().string());
		throw std::runtime_error(err);
	}
	return dll.get<F>(function_name.data());
}

template <typename T> [[nodiscard]]
auto get_symbol(const boost::dll::shared_library& dll, std::string_view symbol_name) -> T& {
	if (!has(dll, symbol_name)) {
		const auto err = std::format(
			"No symbol '{}' in library: '{}'",
			symbol_name,
			dll.location().string());
		throw std::runtime_error(err);
	}
	return dll.get<T>(symbol_name.data());
}

template <typename F>
auto get(const boost::dll::shared_library& dll, std::string_view function_name, std::function<F>* out) -> void {
	*out = get_fn<F>(dll, function_name);
}

template <typename T>
auto get(const boost::dll::shared_library& dll, std::string_view function_name, T** out) -> void {
	*out = &get_symbol<T>(dll, function_name);
}

} // dll
