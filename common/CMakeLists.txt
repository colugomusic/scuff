cmake_minimum_required(VERSION 3.20)
project(scuff-common)
include(../CPM.cmake)
if(UNIX AND NOT APPLE)
	set(LINUX TRUE)
else()
	set(LINUX FALSE)
endif()
list (APPEND sources
	$<$<BOOL:${APPLE}>:${CMAKE_CURRENT_LIST_DIR}/src/common-os-mac.mm>
	$<$<BOOL:${LINUX}>:${CMAKE_CURRENT_LIST_DIR}/src/common-os-lin.cpp>
	$<$<BOOL:${WIN32}>:${CMAKE_CURRENT_LIST_DIR}/src/common-os-win.cpp>
)
CPMAddPackage(NAME clap          GITHUB_REPOSITORY free-audio/clap           GIT_TAG c2c1dea          DOWNLOAD_ONLY YES)
CPMAddPackage(NAME cs_libguarded GITHUB_REPOSITORY copperspice/cs_libguarded GIT_TAG libguarded-1.4.1 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME flux          GITHUB_REPOSITORY tcbrindle/flux            GIT_TAG v0.4.0           DOWNLOAD_ONLY YES)
add_library(scuff-common INTERFACE)
add_library(scuff::common ALIAS scuff-common)
target_sources(scuff-common INTERFACE ${sources})
target_compile_features(scuff-common INTERFACE
	cxx_std_20
)
target_include_directories(scuff-common INTERFACE
	${clap_SOURCE_DIR}/include
	${cs_libguarded_SOURCE_DIR}/src
	${flux_SOURCE_DIR}/include
	${CMAKE_CURRENT_LIST_DIR}/include
)