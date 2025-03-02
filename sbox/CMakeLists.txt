cmake_minimum_required(VERSION 3.20)
project(scuff-sbox)
include(../common/util.cmake)
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
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS headers program_options CONFIG)
add_subdirectory_if_target_doesnt_already_exist(common                   scuff::common::sources)
add_subdirectory_if_target_doesnt_already_exist(extern/fulog             fulog::fulog)
add_subdirectory_if_target_doesnt_already_exist(extern/tga               tga::tga)
add_subdirectory_if_target_doesnt_already_exist(extern/edwin             edwin::edwin)
add_subdirectory_if_target_doesnt_already_exist(extern/ez                ez::ez)
add_subdirectory_if_target_doesnt_already_exist(extern/readerwriterqueue readerwriterqueue)
add_executable(scuff-sbox WIN32 ${scuff-sbox-src})
add_executable(scuff::sbox ALIAS scuff-sbox)
include(../extern/cmrc/CMakeRC.cmake)
cmrc_add_resource_library(scuff-sbox-resources ALIAS scuff::sbox::rc NAMESPACE scuff::sbox ${scuff-sbox-res})
target_include_directories(scuff-sbox PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	../extern/immer
)
target_link_libraries(scuff-sbox PRIVATE
	Boost::headers
	Boost::program_options
	edwin::edwin
	ez::ez
	fulog::fulog
	readerwriterqueue
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
