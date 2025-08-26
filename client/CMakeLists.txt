cmake_minimum_required(VERSION 3.20)
project(scuff-client)
include(../common/util.cmake)
option (SCUFF_BUILD_CLIENT_TESTS "Build the client tests" OFF)
find_package(Boost 1.86.0 REQUIRED COMPONENTS filesystem headers process program_options CONFIG)
find_package(clap REQUIRED CONFIG)
find_package(ez REQUIRED CONFIG)
find_package(fulog REQUIRED CONFIG)
find_package(Immer REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(readerwriterqueue REQUIRED CONFIG)
add_subdirectory_if_target_doesnt_already_exist(common scuff::common::sources)
add_subdirectory_if_target_doesnt_already_exist(sbox   scuff::sbox)
add_subdirectory_if_target_doesnt_already_exist(scan   scuff::scan)
set(sbox_target_file $<TARGET_FILE:scuff::sbox>)	
set(scan_target_file $<TARGET_FILE:scuff::scan>)
source_group(common REGULAR_EXPRESSION [[scuff/common/]])
# Library ######################################################################
add_library(scuff-client STATIC ${scuff-client-src})
add_library(scuff::client ALIAS scuff-client)
target_sources(scuff-client PUBLIC
	FILE_SET HEADERS
	BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
	FILES
		${CMAKE_CURRENT_SOURCE_DIR}/include/scuff/client.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/include/scuff/device-state.hpp
		${CMAKE_CURRENT_SOURCE_DIR}/include/scuff/managed.hpp
)
target_sources(scuff-client PRIVATE
	src/client.cpp
	src/data.hpp
	src/scan.hpp
	src/ui.hpp
	src/ui-types.hpp
)
target_include_directories(scuff-client PRIVATE
	include/scuff
	../extern/subprocess
	${CMAKE_CURRENT_LIST_DIR}/..
	${CMAKE_CURRENT_LIST_DIR}/../common
)
target_link_libraries(scuff-client INTERFACE
	scuff::common::headers
)
target_link_libraries(scuff-client PRIVATE
	$<BUILD_INTERFACE:
		Boost::filesystem
		Boost::headers
		Boost::process
		clap
		ez::ez
		fulog::fulog
		immer
		nlohmann_json::nlohmann_json
		readerwriterqueue::readerwriterqueue
		scuff::common::sources
	>
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
set_target_properties(scuff-client PROPERTIES EXPORT_NAME client)
# Install ######################################################################
include(CMakePackageConfigHelpers)
install(TARGETS scuff-client EXPORT scuff-client-targets FILE_SET HEADERS DESTINATION include/scuff)
install(EXPORT scuff-client-targets FILE scuff-client-targets.cmake NAMESPACE scuff:: DESTINATION lib/cmake/scuff-client)
configure_package_config_file(
	"${CMAKE_CURRENT_LIST_DIR}/cmake/scuff-client-config.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/scuff-client-config.cmake"
	INSTALL_DESTINATION lib/cmake/scuff-client
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/scuff-client-config.cmake" DESTINATION lib/cmake/scuff-client)
# Tests ########################################################################
if (SCUFF_BUILD_CLIENT_TESTS)
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
		EXPORT_NAME client
	)
	target_compile_definitions(scuff-client-test PRIVATE
		SBOX_EXE_PATH="${sbox_target_file}"
		SCAN_EXE_PATH="${scan_target_file}"
	)
endif()