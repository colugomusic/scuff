cmake_minimum_required(VERSION 3.20)
project(scuff-client)
include(../CPM.cmake)
list (APPEND src
	../common/audio_sync.hpp
	../common/c_constants.h
	../common/c_events.h
	../common/c_string_options.h
	../common/events.hpp
	../common/messages.hpp
	../common/os.hpp
	../common/shm.hpp
	../common/types.hpp
	../common/slot_buffer.hpp
	include/scuff/client.h
	src/client.cpp
)
set(BOOST_LIBS_OPTIONAL header CACHE STRING "Boost libraries to be compiled" FORCE)
set(immer_BUILD_TESTS OFF)
set(immer_BUILD_EXAMPLES OFF)
set(immer_BUILD_DOCS OFF)
set(immer_BUILD_EXTRAS OFF)

CPMAddPackage(NAME boost-cmake       GITHUB_REPOSITORY colugomusic/boost-cmake      GIT_TAG master                            )
CPMAddPackage(NAME clap              GITHUB_REPOSITORY free-audio/clap              GIT_TAG c2c1dea          DOWNLOAD_ONLY YES)
CPMAddPackage(NAME clog              GITHUB_REPOSITORY colugomusic/clog             GIT_TAG master           DOWNLOAD_ONLY YES)
CPMAddPackage(NAME cs_libguarded     GITHUB_REPOSITORY copperspice/cs_libguarded    GIT_TAG libguarded-1.4.1 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME ent               GITHUB_REPOSITORY colugomusic/ent              GIT_TAG main                              )
CPMAddPackage(NAME immer             GITHUB_REPOSITORY arximboldi/immer             GIT_TAG v0.8.1                            )
CPMAddPackage(NAME readerwriterqueue GITHUB_REPOSITORY cameron314/readerwriterqueue GIT_TAG v1.0.6                            )
add_library(${PROJECT_NAME} STATIC ${src})
set_target_properties(${PROJECT_NAME} PROPERTIES
	CXX_STANDARD 20
)
target_include_directories(${PROJECT_NAME} PUBLIC
	include/scuff
	${clap_SOURCE_DIR}/include
	${clog_SOURCE_DIR}/include
	${CMAKE_CURRENT_LIST_DIR}/..
	${cs_libguarded_SOURCE_DIR}/src
)
target_link_libraries(${PROJECT_NAME} PUBLIC
	Boost::boost
	ent
	immer
	readerwriterqueue
)
