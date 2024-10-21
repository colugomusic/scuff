cmake_minimum_required(VERSION 3.20)
project(scuff-client)
if (NOT TARGET scuff::common)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
endif()
include(../CPM.cmake)
list (APPEND src
	include/scuff/client.hpp
	include/scuff/device-state.hpp
	include/scuff/managed.hpp
	src/client.cpp
	src/data.hpp
	src/scan.hpp
	src/ui.hpp
	src/ui-types.hpp
)
set(immer_BUILD_TESTS OFF)
set(immer_BUILD_EXAMPLES OFF)
set(immer_BUILD_DOCS OFF)
set(immer_BUILD_EXTRAS OFF)
find_package(Boost REQUIRED COMPONENTS filesystem headers)
CPMAddPackage(NAME clap              GITHUB_REPOSITORY free-audio/clap              GIT_TAG c2c1dea          DOWNLOAD_ONLY YES)
CPMAddPackage(NAME ent               GITHUB_REPOSITORY colugomusic/ent              GIT_TAG main                              )
CPMAddPackage(NAME immer             GITHUB_REPOSITORY arximboldi/immer             GIT_TAG v0.8.1                            )
CPMAddPackage(NAME json              GITHUB_REPOSITORY nlohmann/json                GIT_TAG v3.11.3                           )
CPMAddPackage(NAME readerwriterqueue GITHUB_REPOSITORY cameron314/readerwriterqueue GIT_TAG v1.0.6                            )
add_library(scuff-client STATIC ${src})
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
target_link_libraries(scuff-client PUBLIC
	Boost::filesystem
	Boost::headers
	ent
	immer
	nlohmann_json::nlohmann_json
	readerwriterqueue
	scuff::common
)
target_compile_features(scuff-client PUBLIC
	cxx_std_20
)
target_compile_options(scuff-client PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
