#pragma once

#include <string>
#include <vector>

#if defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <boost/process.hpp>
#include <boost/process/v1/windows.hpp>

namespace bp = boost::process;

namespace scuff::os {

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::string& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, bp::windows::hide, std::forward<BoostArgs>(boost_args)...};
}

} // scuff::os

#elif defined(__APPLE__)

#include <unistd.h>
#include <boost/process.hpp>

namespace bp = boost::process;

namespace scuff::os {

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::string& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, std::forward<BoostArgs>(boost_args)...};
}

} // scuff::os

#elif defined(__linux__)

#include <unistd.h>
#include <boost/process.hpp>

namespace bp = boost::process;

namespace scuff::os {

template<typename... BoostArgs> [[nodiscard]] static
auto start_child_process(const std::string& exe, const std::vector<std::string>& args, BoostArgs&&... boost_args) -> bp::child {
	return bp::child{exe, args, std::forward<BoostArgs>(boost_args)...};
}

} // scuff::os

#endif
