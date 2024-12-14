cmake_minimum_required(VERSION 3.20)
project(scuff-client)
set(immer_BUILD_TESTS OFF)
set(immer_BUILD_EXAMPLES OFF)
set(immer_BUILD_DOCS OFF)
set(immer_BUILD_EXTRAS OFF)
if (NOT TARGET scuff::common::sources)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
endif()
if (NOT TARGET scuff::sbox)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../sbox ${CMAKE_BINARY_DIR}/sbox)
endif()
if (NOT TARGET scuff::scan)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../scan ${CMAKE_BINARY_DIR}/scan)
endif()
set(sbox_target_file $<TARGET_FILE:scuff::sbox>)	
set(scan_target_file $<TARGET_FILE:scuff::scan>)
include(../CPM.cmake)
find_package(Boost COMPONENTS filesystem headers program_options REQUIRED)
CPMAddPackage(NAME clap              GITHUB_REPOSITORY free-audio/clap              GIT_TAG c2c1dea DOWNLOAD_ONLY YES)
CPMAddPackage(NAME ez                GITHUB_REPOSITORY colugomusic/ez               GIT_TAG master                   )
CPMAddPackage(NAME immer             GITHUB_REPOSITORY arximboldi/immer             GIT_TAG v0.8.1                   )
CPMAddPackage(NAME json              GITHUB_REPOSITORY nlohmann/json                GIT_TAG v3.11.3                  )
CPMAddPackage(NAME PlatformFolders   GITHUB_REPOSITORY sago007/PlatformFolders      GIT_TAG 4.2.0                    )
CPMAddPackage(NAME readerwriterqueue GITHUB_REPOSITORY cameron314/readerwriterqueue GIT_TAG v1.0.6                   )
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
	${clap_SOURCE_DIR}/include
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
