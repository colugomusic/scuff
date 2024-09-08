cmake_minimum_required(VERSION 3.20)
project(scuff-sbox)
include(../CPM.cmake)
list (APPEND scuff-sbox_SRCFILES
	../common/messages.hpp
	../common/shm.hpp
	src/app.hpp
	src/cmdline.hpp
	src/device.cpp
	src/device.hpp
	src/dll.hpp
	src/main.cpp
	src/options.hpp
	src/plugfile.cpp
	src/plugfile.hpp
	src/shm.cpp
	src/shm.hpp
)
set(BOOST_LIBS_OPTIONAL
	header
	CACHE STRING "Boost libraries to be compiled"
	FORCE
)
CPMAddPackage(NAME boost-cmake GITHUB_REPOSITORY colugomusic/boost-cmake GIT_TAG master)
CPMAddPackage(NAME clap        GITHUB_REPOSITORY free-audio/clap         GIT_TAG c2c1dea DOWNLOAD_ONLY YES)
CPMAddPackage(NAME ent         GITHUB_REPOSITORY colugomusic/ent         GIT_TAG main)
CPMAddPackage(NAME nappgui     GITHUB_REPOSITORY frang75/nappgui_src     GIT_TAG v1.4.2 OPTIONS "NAPPGUI_DEMO OFF" "NAPPGUI_WEB OFF")
set(NAPPGUI_ROOT_PATH ${nappgui_SOURCE_DIR})
nap_generate_tools()
nap_desktop_app(scuff-sbox osapp NRC_EMBEDDED)
nap_target_cxx_standard(scuff-sbox 20)
target_include_directories(scuff-sbox PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	${clap_SOURCE_DIR}/include
)
target_link_libraries(scuff-sbox
	Boost::boost
	ent
	${NAPPGUI_LIBRARIES}
)
target_compile_options(scuff-sbox PRIVATE
	$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
