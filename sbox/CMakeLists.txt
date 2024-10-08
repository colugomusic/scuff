cmake_minimum_required(VERSION 3.20)
project(scuff-sbox)
if (NOT TARGET scuff::common)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
endif()
include(../CPM.cmake)
list (APPEND scuff-sbox_SRCFILES
	src/clap.hpp
	src/clap_data.hpp
	src/cmdline.hpp
	src/data.hpp
	src/debug_ui.hpp
	src/main.cpp
	src/msg-proc.hpp
	src/sbox-signaling.hpp
	src/options.hpp
)
#set(CPM_nappgui_SOURCE "Z:/dv/nappgui_src")
set(SCUFF_SIGNALING_MODE "boost-ipc" CACHE STRING "Signalling mode")
find_package(Boost REQUIRED COMPONENTS filesystem headers)
CPMAddPackage(NAME clap        GITHUB_REPOSITORY free-audio/clap              GIT_TAG c2c1dea  DOWNLOAD_ONLY YES                           )
CPMAddPackage(NAME ent         GITHUB_REPOSITORY colugomusic/ent              GIT_TAG main                                                 )
CPMAddPackage(NAME immer       GITHUB_REPOSITORY arximboldi/immer             GIT_TAG v0.8.1                                               )
CPMAddPackage(NAME nappgui     GITHUB_REPOSITORY colugomusic/nappgui_src      GIT_TAG main     OPTIONS "NAPPGUI_DEMO OFF" "NAPPGUI_WEB OFF" "CMAKE_DISABLE_CRTDBG ON")
CPMAddPackage(Name rwq         GITHUB_REPOSITORY cameron314/readerwriterqueue GIT_TAG v1.0.6                                               )
set(NAPPGUI_ROOT_PATH ${nappgui_SOURCE_DIR})
nap_generate_tools()
nap_desktop_app(scuff-sbox "osapp" NRC_EMBEDDED)
nap_target_cxx_standard(scuff-sbox 20)
add_executable(scuff::sbox ALIAS scuff-sbox)
target_include_directories(scuff-sbox PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	${clap_SOURCE_DIR}/include
)
target_link_libraries(scuff-sbox
	Boost::filesystem
	Boost::headers
	ent
	immer
	readerwriterqueue
	scuff::common
)
target_compile_options(scuff-sbox PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
target_compile_definitions(scuff-sbox PRIVATE
	$<$<STREQUAL:${SCUFF_SIGNALING_MODE},boost-ipc>:SCUFF_SIGNALING_MODE_BOOST_IPC>
)
