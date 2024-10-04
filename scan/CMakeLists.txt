cmake_minimum_required(VERSION 3.20)
project(scuff-scan)
if (NOT TARGET scuff::common)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
endif()
include(../CPM.cmake)
list (APPEND src
	src/main.cpp
)
find_package(Boost REQUIRED COMPONENTS filesystem headers)
CPMAddPackage(NAME cxxopts     GITHUB_REPOSITORY jarro2783/cxxopts       GIT_TAG v3.2.1                   )
CPMAddPackage(NAME json        GITHUB_REPOSITORY nlohmann/json           GIT_TAG v3.11.3                  )
add_executable(scuff-scan ${src})
set_target_properties(scuff-scan PROPERTIES CXX_STANDARD 20)
target_link_libraries(scuff-scan
	Boost::filesystem
	Boost::headers
	cxxopts::cxxopts
	nlohmann_json::nlohmann_json
	scuff::common
)
target_compile_options(scuff-scan PRIVATE
	$<$<CXX_COMPILER_ID:GNU,Clang>:-Werror>
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
