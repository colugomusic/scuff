cmake_minimum_required(VERSION 3.20)
project(scuff-sbox)
if (NOT TARGET scuff::common::sources)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
endif()
include(../CPM.cmake)
list (APPEND src
	src/audio.hpp
	src/clap.hpp
	src/clap-data.hpp
	src/cmdline.hpp
	src/data.hpp
	src/gui.hpp
	src/loguru.cpp
	src/loguru.hpp
	src/main.cpp
	src/msg-proc.hpp
	src/op.hpp
	src/options.hpp
	src/os.hpp
	$<$<BOOL:${APPLE}>:src/os-mac.mm>
	$<$<BOOL:${LINUX}>:src/os-lin.cpp>
	$<$<BOOL:${WIN32}>:src/os-win.cpp>
)
set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)
find_package(Boost COMPONENTS headers program_options REQUIRED)
CPMAddPackage(NAME clap            GITHUB_REPOSITORY free-audio/clap              GIT_TAG c2c1dea  DOWNLOAD_ONLY YES)
CPMAddPackage(NAME edwin           GITHUB_REPOSITORY colugomusic/edwin            GIT_TAG master                    )
CPMAddPackage(NAME ez              GITHUB_REPOSITORY colugomusic/ez               GIT_TAG master                    )
CPMAddPackage(NAME immer           GITHUB_REPOSITORY arximboldi/immer             GIT_TAG v0.8.1                    )
CPMAddPackage(NAME PlatformFolders GITHUB_REPOSITORY sago007/PlatformFolders      GIT_TAG 4.2.0                     )
CPMAddPackage(Name rwq             GITHUB_REPOSITORY cameron314/readerwriterqueue GIT_TAG v1.0.6                    )
add_executable(scuff-sbox ${src})
add_executable(scuff::sbox ALIAS scuff-sbox)
target_include_directories(scuff-sbox PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	${clap_SOURCE_DIR}/include
)
target_link_libraries(scuff-sbox PRIVATE
	Boost::headers
	Boost::program_options
	edwin::edwin
	ez::ez
	immer
	readerwriterqueue
	sago::platform_folders
	scuff::common::headers
	scuff::common::sources
)
target_compile_options(scuff-sbox PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
set_target_properties(scuff-sbox PROPERTIES
	CXX_STANDARD 20
)
target_compile_definitions(scuff-sbox PRIVATE
	LOGURU_WITH_STREAMS=1
)
