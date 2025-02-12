cmake_minimum_required(VERSION 3.20)
project(scuff-scan)
include(../common/util.cmake)
list (APPEND src
	src/main.cpp
)
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS headers program_options REQUIRED)
add_subdirectory_if_target_doesnt_already_exist(common                   scuff::common::sources)
add_subdirectory_if_target_doesnt_already_exist(extern/json              nlohmann_json::nlohmann_json)
add_executable(scuff-scan WIN32 ${src})
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
source_group(common REGULAR_EXPRESSION [[scuff/common/]])
