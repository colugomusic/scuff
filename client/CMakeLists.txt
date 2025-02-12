cmake_minimum_required(VERSION 3.20)
project(scuff-client)
include(../common/util.cmake)
set(immer_BUILD_TESTS OFF)
set(immer_BUILD_EXAMPLES OFF)
set(immer_BUILD_DOCS OFF)
set(immer_BUILD_EXTRAS OFF)
if (APPLE)
set(Boost_USE_STATIC_LIBS ON)
endif()
find_package(Boost 1.86.0 COMPONENTS filesystem headers program_options REQUIRED)
add_subdirectory_if_target_doesnt_already_exist(common                   scuff::common::sources)
add_subdirectory_if_target_doesnt_already_exist(sbox                     scuff::sbox)
add_subdirectory_if_target_doesnt_already_exist(scan                     scuff::scan)
add_subdirectory_if_target_doesnt_already_exist(extern/ez                ez::ez)
add_subdirectory_if_target_doesnt_already_exist(extern/immer             immer)
add_subdirectory_if_target_doesnt_already_exist(extern/json              nlohmann_json::nlohmann_json)
add_subdirectory_if_target_doesnt_already_exist(extern/readerwriterqueue readerwriterqueue)
add_subdirectory_if_target_doesnt_already_exist(extern/PlatformFolders   sago::platform_folders)
set(sbox_target_file $<TARGET_FILE:scuff::sbox>)	
set(scan_target_file $<TARGET_FILE:scuff::scan>)
source_group(common REGULAR_EXPRESSION [[scuff/common/]])
# Library ######################################################################
list (APPEND scuff-client-src
	include/scuff/client.hpp
	include/scuff/device-state.hpp
	include/scuff/managed.hpp
	src/client.cpp
	src/data.hpp
	src/scan.hpp
	src/ui.hpp
	src/ui-types.hpp
)
add_library(scuff-client STATIC ${scuff-client-src})
add_library(scuff::client ALIAS scuff-client)
target_include_directories(scuff-client INTERFACE
	include
)
target_include_directories(scuff-client PRIVATE
	include/scuff
	../extern/clap/include
	${CMAKE_CURRENT_LIST_DIR}/..
	${CMAKE_CURRENT_LIST_DIR}/../common
)
target_link_libraries(scuff-client PRIVATE
	Boost::filesystem
	Boost::headers
	ez::ez
	immer
	nlohmann_json::nlohmann_json
	readerwriterqueue
	sago::platform_folders
	scuff::common::sources
)
target_link_libraries(scuff-client PUBLIC
	scuff::common::headers
)
target_compile_features(scuff-client PUBLIC
	cxx_std_20
)
target_compile_options(scuff-client PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
target_compile_definitions(scuff-client PRIVATE
	BOOST_INTERPROCESS_SHARED_DIR_FUNC 
)
# Tests ########################################################################
list (APPEND scuff-client-test-src
	test/src/main.cpp
)
add_executable(scuff-client-test ${scuff-client-test-src})
add_executable(scuff::client::test ALIAS scuff-client-test)
add_dependencies(scuff-client-test scuff::sbox)
add_dependencies(scuff-client-test scuff::scan)
target_include_directories(scuff-client-test PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
)
target_link_libraries(scuff-client-test
	Boost::program_options
	scuff::client
)
if (APPLE)
target_link_libraries(scuff-client-test "-framework Foundation" "-framework CoreData")
endif()
target_compile_options(scuff-client-test PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
set_target_properties(scuff-client-test PROPERTIES
	CXX_STANDARD 20
)
target_compile_definitions(scuff-client-test PRIVATE
	SBOX_EXE_PATH="${sbox_target_file}"
	SCAN_EXE_PATH="${scan_target_file}"
)
