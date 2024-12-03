cmake_minimum_required(VERSION 3.20)
project(scuff-client-test)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/..         ${CMAKE_BINARY_DIR}/client)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../sbox ${CMAKE_BINARY_DIR}/sbox)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../scan ${CMAKE_BINARY_DIR}/scan)
set(sbox_target_file $<TARGET_FILE:scuff::sbox>)	
set(scan_target_file $<TARGET_FILE:scuff::scan>)
list (APPEND scuff-client-test-src
	src/main.cpp
)
find_package(Boost COMPONENTS headers program_options REQUIRED)
add_executable(scuff-client-test ${scuff-client-test-src})
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
