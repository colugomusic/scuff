cmake_minimum_required(VERSION 3.20)
project(scuff-sbox)
if (NOT TARGET scuff::common::sources)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
endif()
include(../CPM.cmake)
list (APPEND scuff-sbox-src
	src/audio.hpp
	src/clap.hpp
	src/clap-data.hpp
	src/cmdline.hpp
	src/data.hpp
	src/gui.hpp
	src/main.cpp
	src/msg-proc.hpp
	src/op.hpp
	src/options.hpp
	src/os.hpp
	$<$<BOOL:${APPLE}>:src/os-mac.mm>
	$<$<BOOL:${LINUX}>:src/os-lin.cpp>
	$<$<BOOL:${WIN32}>:src/os-win.cpp>
)
list (APPEND scuff-sbox-res
	res/clap-icon-256.tga
)
set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)
find_package(Boost COMPONENTS headers program_options REQUIRED)
CPMAddPackage(NAME cmrc            GITHUB_REPOSITORY vector-of-bool/cmrc          GIT_TAG 2.0.1                     )
CPMAddPackage(NAME fulog           GITHUB_REPOSITORY colugomusic/fulog            GIT_TAG master                    )
CPMAddPackage(NAME tga             GITHUB_REPOSITORY colugomusic/tga              GIT_TAG main                      )
CPMAddPackage(NAME clap            GITHUB_REPOSITORY free-audio/clap              GIT_TAG c2c1dea  DOWNLOAD_ONLY YES)
CPMAddPackage(NAME edwin           GITHUB_REPOSITORY colugomusic/edwin            GIT_TAG master                    )
CPMAddPackage(NAME ez              GITHUB_REPOSITORY colugomusic/ez               GIT_TAG master                    )
CPMAddPackage(NAME immer           GITHUB_REPOSITORY arximboldi/immer             GIT_TAG v0.8.1                    )
CPMAddPackage(NAME PlatformFolders GITHUB_REPOSITORY sago007/PlatformFolders      GIT_TAG 4.2.0                     )
CPMAddPackage(Name rwq             GITHUB_REPOSITORY cameron314/readerwriterqueue GIT_TAG v1.0.6                    )
add_executable(scuff-sbox WIN32 ${scuff-sbox-src})
add_executable(scuff::sbox ALIAS scuff-sbox)
cmrc_add_resource_library(scuff-sbox-resources ALIAS scuff::sbox::rc NAMESPACE scuff::sbox ${scuff-sbox-res})
target_include_directories(scuff-sbox PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	${clap_SOURCE_DIR}/include
)
target_link_libraries(scuff-sbox PRIVATE
	Boost::headers
	Boost::program_options
	edwin::edwin
	ez::ez
	fulog::fulog
	immer
	readerwriterqueue
	sago::platform_folders
	scuff::common::headers
	scuff::common::sources
	scuff::sbox::rc
	tga::tga
)
target_compile_options(scuff-sbox PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
set_target_properties(scuff-sbox-resources PROPERTIES
	CXX_STANDARD 20
)
set_target_properties(scuff-sbox PROPERTIES
	CXX_STANDARD 20
)
target_compile_definitions(scuff-sbox PRIVATE
	BOOST_INTERPROCESS_SHARED_DIR_FUNC 
	LOGURU_WITH_STREAMS=1
)
source_group(common REGULAR_EXPRESSION [[scuff/common/]])
