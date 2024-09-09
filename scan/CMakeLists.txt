cmake_minimum_required(VERSION 3.20)
project(scuff-scan)
include(../CPM.cmake)
if(UNIX AND NOT APPLE)
	set(LINUX TRUE)
else()
	set(LINUX FALSE)
endif()
list (APPEND src
	src/constants.hpp
	src/main.cpp
	src/os.hpp
	src/util.cpp
	src/util.hpp
	$<$<BOOL:${APPLE}>:src/os-mac.mm>
	$<$<BOOL:${LINUX}>:src/os-lin.cpp>
	$<$<BOOL:${WIN32}>:src/os-win.cpp>
)
set(BOOST_LIBS_OPTIONAL header CACHE STRING "Boost libraries to be compiled" FORCE)
CPMAddPackage(NAME boost-cmake GITHUB_REPOSITORY colugomusic/boost-cmake GIT_TAG master                   )
CPMAddPackage(NAME clap        GITHUB_REPOSITORY free-audio/clap         GIT_TAG c2c1dea DOWNLOAD_ONLY YES)
CPMAddPackage(NAME cxxopts     GITHUB_REPOSITORY jarro2783/cxxopts       GIT_TAG v3.2.1                   )
CPMAddPackage(NAME flux        GITHUB_REPOSITORY tcbrindle/flux          GIT_TAG v0.4.0                   )
CPMAddPackage(NAME json        GITHUB_REPOSITORY nlohmann/json           GIT_TAG v3.11.3                  )
add_executable(scuff-scan ${src})
set_target_properties(scuff-scan PROPERTIES CXX_STANDARD 20)
target_include_directories(scuff-scan PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	${clap_SOURCE_DIR}/include
)
target_link_libraries(scuff-scan
	Boost::boost
	cxxopts::cxxopts
	flux::flux
	nlohmann_json::nlohmann_json
)
target_compile_options(scuff-scan PRIVATE
	$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
