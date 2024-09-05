cmake_minimum_required(VERSION 3.20)
project(scuff-scanner)
include(../CPM.cmake)
list (APPEND src
	src/main.cpp
)
set(BOOST_LIBS_OPTIONAL header CACHE STRING "Boost libraries to be compiled" FORCE)
CPMAddPackage(NAME boost-cmake GITHUB_REPOSITORY colugomusic/boost-cmake GIT_TAG master)
CPMAddPackage(NAME clap        GITHUB_REPOSITORY free-audio/clap         GIT_TAG c2c1dea DOWNLOAD_ONLY YES)
add_executable(scuff-scanner ${src})
target_include_directories(scuff-scanner PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	${clap_SOURCE_DIR}/include
)
target_link_libraries(scuff-scanner
	Boost::boost
)
