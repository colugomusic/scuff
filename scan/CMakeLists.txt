cmake_minimum_required(VERSION 3.20)
project(scuff-scan)
if (NOT TARGET scuff::common::sources)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
endif()
include(../CPM.cmake)
list (APPEND src
	src/main.cpp
)
find_package(Boost COMPONENTS headers program_options REQUIRED)
CPMAddPackage(NAME json GITHUB_REPOSITORY nlohmann/json GIT_TAG v3.11.3)
add_executable(scuff-scan ${src})
add_executable(scuff::scan ALIAS scuff-scan)
set_target_properties(scuff-scan PROPERTIES CXX_STANDARD 20)
target_link_libraries(scuff-scan
	Boost::headers
	Boost::program_options
	nlohmann_json::nlohmann_json
	scuff::common::headers
	scuff::common::sources
)
target_compile_options(scuff-scan PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
