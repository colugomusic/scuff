cmake_minimum_required(VERSION 3.20)
project(scuff-scan)
include(../common/util.cmake)
find_package(Boost REQUIRED COMPONENTS headers program_options CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
add_subdirectory_if_target_doesnt_already_exist(common scuff::common::sources)
add_executable(scuff-scan WIN32 ${src})
add_executable(scuff::scan ALIAS scuff-scan)
target_sources(scuff-scan PRIVATE
	src/main.cpp
)
set_target_properties(scuff-scan PROPERTIES CXX_STANDARD 20)
target_link_libraries(scuff-scan
	Boost::headers
	Boost::program_options
	nlohmann_json::nlohmann_json
	scuff::common::headers
	scuff::common::sources
)
if (APPLE)
	target_link_libraries(scuff-scan "-framework Foundation" "-framework CoreFoundation")
endif()
target_compile_options(scuff-scan PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
source_group(common REGULAR_EXPRESSION [[scuff/common/]])