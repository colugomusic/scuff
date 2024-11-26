cmake_minimum_required(VERSION 3.20)
project(scuff-test-host)
include(../CPM.cmake)
list (APPEND scuff-test-host_SRCFILES
	src/main.cpp
)
set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)
CPMAddPackage(NAME nappgui_src GITHUB_REPOSITORY colugomusic/nappgui_src GIT_TAG main  OPTIONS "NAPPGUI_DEMO OFF" "NAPPGUI_WEB OFF" "CMAKE_DISABLE_CRTDBG ON")
set(NAPPGUI_ROOT_PATH ${nappgui_src_SOURCE_DIR})
nap_generate_tools()
nap_desktop_app(scuff-test-host "osapp" NRC_NONE)
nap_target_cxx_standard(scuff-test-host 20)
target_include_directories(scuff-test-host PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
)
target_link_libraries(scuff-test-host
	scuff::client
)
if (APPLE)
    target_link_libraries(scuff-test-host
        "-framework Foundation"
        "-framework Cocoa"
        "-framework UniformTypeIdentifiers"
    )
endif()
target_compile_options(scuff-test-host PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
