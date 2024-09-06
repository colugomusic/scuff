cmake_minimum_required(VERSION 3.20)
project(scuff-scanner)
include(../CPM.cmake)
list (APPEND src
	src/main.cpp
)
set(BOOST_LIBS_OPTIONAL header CACHE STRING "Boost libraries to be compiled" FORCE)
CPMAddPackage(NAME cxxopts     GITHUB_REPOSITORY jarro2783/cxxopts       GIT_TAG v3.2.1)
CPMAddPackage(NAME boost-cmake GITHUB_REPOSITORY colugomusic/boost-cmake GIT_TAG master)
CPMAddPackage(NAME clap        GITHUB_REPOSITORY free-audio/clap         GIT_TAG c2c1dea DOWNLOAD_ONLY YES)
add_executable(scuff-scanner ${src})
set_target_properties(scuff-scanner PROPERTIES CXX_STANDARD 20)
target_include_directories(scuff-scanner PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	${clap_SOURCE_DIR}/include
)
target_link_libraries(scuff-scanner
	Boost::boost
	cxxopts::cxxopts
)
