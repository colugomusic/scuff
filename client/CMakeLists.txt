cmake_minimum_required(VERSION 3.20)
project(scuff-client)
include(../CPM.cmake)
list (APPEND src
	../common/audio_sync.hpp
	../common/c_constants.h
	../common/c_events.h
	../common/c_plugin_type.h
	../common/c_render_mode.h
	../common/c_types.h
	../common/events.hpp
	../common/messages.hpp
	../common/os.hpp
	../common/serialization.hpp
	../common/shm.hpp
	../common/types.hpp
	../common/slot_buffer.hpp
	include/scuff/client.h
	src/client.cpp
	src/data.hpp
	src/scan.hpp
)
set(BOOST_LIBS_OPTIONAL header filesystem system regex CACHE STRING "Boost libraries to be compiled" FORCE)
set(immer_BUILD_TESTS OFF)
set(immer_BUILD_EXAMPLES OFF)
set(immer_BUILD_DOCS OFF)
set(immer_BUILD_EXTRAS OFF)

CPMAddPackage(NAME boost-cmake       GITHUB_REPOSITORY colugomusic/boost-cmake      GIT_TAG master                            )
CPMAddPackage(NAME clap              GITHUB_REPOSITORY free-audio/clap              GIT_TAG c2c1dea          DOWNLOAD_ONLY YES)
CPMAddPackage(NAME cs_libguarded     GITHUB_REPOSITORY copperspice/cs_libguarded    GIT_TAG libguarded-1.4.1 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME ent               GITHUB_REPOSITORY colugomusic/ent              GIT_TAG main                              )
CPMAddPackage(NAME immer             GITHUB_REPOSITORY arximboldi/immer             GIT_TAG v0.8.1                            )
CPMAddPackage(NAME json              GITHUB_REPOSITORY nlohmann/json                GIT_TAG v3.11.3                           )
CPMAddPackage(NAME readerwriterqueue GITHUB_REPOSITORY cameron314/readerwriterqueue GIT_TAG v1.0.6                            )
add_library(scuff-client STATIC ${src})
add_library(scuff-client::scuff-client ALIAS scuff-client)
set_target_properties(scuff-client PROPERTIES
	CXX_STANDARD 20
)
target_include_directories(scuff-client INTERFACE
	include
	${CMAKE_CURRENT_LIST_DIR}/../common
)
target_include_directories(scuff-client PRIVATE
	include/scuff
	${clap_SOURCE_DIR}/include
	${CMAKE_CURRENT_LIST_DIR}/..
	${CMAKE_CURRENT_LIST_DIR}/../common
	${cs_libguarded_SOURCE_DIR}/src
)
target_link_libraries(scuff-client PUBLIC
	Boost::boost
	Boost::filesystem
	Boost::regex
	ent
	immer
	nlohmann_json::nlohmann_json
	readerwriterqueue
)
target_compile_options(scuff-client PRIVATE
	$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
