cmake_minimum_required(VERSION 3.20)
project(scuff-common)
if(UNIX AND NOT APPLE)
	set(LINUX TRUE)
else()
	set(LINUX FALSE)
endif()
list (APPEND scuff-common-hdr
	${CMAKE_CURRENT_LIST_DIR}/include/common-clap.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-colors.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-constants.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-device-info.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-event-buffer.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-events.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-events-clap.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-ipc-event.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-messages.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-message-send-rcv.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-os.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-os-child-proc.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-os-dso.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-param-info.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-plugin-type.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-render-mode.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-serialize.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-serialize-events.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-serialize-messages.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-serialize-param-info.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-shm.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-signaling.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-slot-buffer.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-types.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-util.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/common-visit.hpp
	${CMAKE_CURRENT_LIST_DIR}/include/jthread.hpp
)
list (APPEND scuff-common-src
	$<$<BOOL:${APPLE}>:${CMAKE_CURRENT_LIST_DIR}/src/common-os-mac.mm>
	$<$<BOOL:${LINUX}>:${CMAKE_CURRENT_LIST_DIR}/src/common-os-lin.cpp>
	$<$<BOOL:${WIN32}>:${CMAKE_CURRENT_LIST_DIR}/src/common-os-win.cpp>
)
add_library(scuff-common-headers INTERFACE)
add_library(scuff-common-sources INTERFACE)
add_library(scuff::common::headers ALIAS scuff-common-headers)
add_library(scuff::common::sources ALIAS scuff-common-sources)
target_sources(scuff-common-headers INTERFACE ${scuff-common-hdr})
target_sources(scuff-common-sources INTERFACE ${scuff-common-src})
target_compile_features(scuff-common-headers INTERFACE cxx_std_20)
target_compile_features(scuff-common-sources INTERFACE cxx_std_20)
target_include_directories(scuff-common-headers INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/include
)
target_include_directories(scuff-common-sources INTERFACE
	../extern/clap/include
	../extern/cs_libguarded/src
	../extern/flux/include
)