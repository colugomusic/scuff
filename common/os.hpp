#pragma once

#if defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <boost/process.hpp>
#include <boost/process/windows.hpp>

namespace bp = boost::process;

namespace scuff::os {

[[nodiscard]] static
auto get_process_id() -> int {
	return int(GetCurrentProcessId());
}

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::string& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, bp::windows::hide, std::forward<BoostArgs>(boost_args)...};
}

} // scuff::os

#elif defined(__APPLE__)

#include <unistd.h>
#include <boost/process.hpp>

namespace scuff::os {

[[nodiscard]] static
auto get_process_id() -> int {
	return getpid();
}

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::filesystem::path& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, std::forward<BoostArgs>(boost_args)...};
}

} // scuff::os

#elif defined(__linux__)

#include <unistd.h>
#include <boost/process.hpp>

namespace scuff::os {

j[[nodiscard]] static
auto get_process_id() -> int {
	return getpid();
}

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::filesystem::path& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, std::forward<BoostArgs>(boost_args)...};
}

} // scuff::os

#endif
