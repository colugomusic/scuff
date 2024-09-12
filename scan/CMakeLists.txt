cmake_minimum_required(VERSION 3.20)
project(scuff-scan)
include(../CPM.cmake)
list (APPEND src
	src/main.cpp
)
set(BOOST_LIBS_OPTIONAL header CACHE STRING "Boost libraries to be compiled" FORCE)
CPMAddPackage(NAME boost-cmake GITHUB_REPOSITORY colugomusic/boost-cmake GIT_TAG master                   )
CPMAddPackage(NAME cxxopts     GITHUB_REPOSITORY jarro2783/cxxopts       GIT_TAG v3.2.1                   )
CPMAddPackage(NAME json        GITHUB_REPOSITORY nlohmann/json           GIT_TAG v3.11.3                  )
add_executable(scuff-scan ${src})
set_target_properties(scuff-scan PROPERTIES CXX_STANDARD 20)
target_link_libraries(scuff-scan
	Boost::boost
	cxxopts::cxxopts
	nlohmann_json::nlohmann_json
	scuff::common
)
target_compile_options(scuff-scan PRIVATE
	$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
