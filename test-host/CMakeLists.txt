cmake_minimum_required(VERSION 3.20)
project(scuff-test-host)
include(../CPM.cmake)
list (APPEND scuff-test-host_SRCFILES
	src/main.cpp
)
CPMAddPackage(NAME nappgui GITHUB_REPOSITORY frang75/nappgui_src GIT_TAG b65fc31  OPTIONS "NAPPGUI_DEMO OFF" "NAPPGUI_WEB OFF" "CMAKE_DISABLE_CRTDBG ON")
set(NAPPGUI_ROOT_PATH ${nappgui_SOURCE_DIR})
nap_generate_tools()
nap_desktop_app(scuff-test-host osapp NRC_NONE)
nap_target_cxx_standard(scuff-test-host 20)
target_include_directories(scuff-test-host PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/..
)
target_link_libraries(scuff-test-host
	${NAPPGUI_LIBRARIES}
	scuff::client
)
target_compile_options(scuff-test-host PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W3 /WX>
)
include_directories(BEFORE /usr/include)
