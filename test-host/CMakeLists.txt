cmake_minimum_required(VERSION 3.20)
project(scuff-test-host)
include(../CPM.cmake)
list (APPEND scuff-test-host_SRCFILES
	src/main.cpp
)
CPMAddPackage(NAME cs_libguarded GITHUB_REPOSITORY copperspice/cs_libguarded GIT_TAG libguarded-1.4.1 DOWNLOAD_ONLY YES)
CPMAddPackage(NAME nappgui       GITHUB_REPOSITORY frang75/nappgui_src       GIT_TAG v1.4.2           OPTIONS "NAPPGUI_DEMO OFF" "NAPPGUI_WEB OFF")
set(NAPPGUI_ROOT_PATH ${nappgui_SOURCE_DIR})
nap_generate_tools()
nap_desktop_app(scuff-test-host osapp NRC_NONE)
nap_target_cxx_standard(scuff-test-host 20)
target_include_directories(scuff-test-host PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
	${cs_libguarded_SOURCE_DIR}/src
)
target_link_libraries(scuff-test-host
	${NAPPGUI_LIBRARIES}
	scuff-client::scuff-client
)
target_compile_options(scuff-test-host PRIVATE
	$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
