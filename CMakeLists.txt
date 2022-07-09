cmake_minimum_required(VERSION 3.1.0)

project(trancecreator)

set (CMAKE_CXX_STANDARD 17)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(wxWidgets REQUIRED COMPONENTS net core base)  # from the website
include(${wxWidgets_USE_FILE})

find_package(PkgConfig)

include_directories("${CMAKE_SOURCE_DIR}")
include_directories("${CMAKE_SOURCE_DIR}/src")

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
#include (src/common/proto.cmake)

##
## Try to find the VPX library from webm, /usr/lib/x86_64-linux-gnu/pkgconfig/vpx.pc
##
include (cmake.d/FindVpxLibrary.cmake)

include (cmake.d/FindWebMLibrary.cmake)

find_package(GIF REQUIRED)

SET (TRANCE_CREATOR_SOURCES
    src/common/media/image.cpp
    src/common/media/streamer.cpp
    src/common/session.cpp
    src/creator/export.cpp
	src/creator/launch.cpp
    src/creator/main.cpp
    src/creator/playlist.cpp
    src/creator/program.cpp
    src/creator/settings.cpp
    src/creator/theme.cpp
    src/creator/variables.cpp
    src/jpgd/jpgd.cpp
    )
    
SET (TRANCE_CREATOR_HEADERS
	 src/common/common.h
     src/common/media/image.h
     src/common/media/streamer.h
      src/common/session.h
      src/common/util.h
      src/creator/export.h
      src/creator/item_list.h
      src/creator/launch.h
      src/creator/main.h
      src/creator/playlist.h
      src/creator/program.h
      src/creator/settings.h
      src/creator/theme.h
      src/creator/variables.h
      src/jpgd/jpgd.h
)

add_executable (trancecreator ${TRANCE_CREATOR_SOURCES} ${TRANCE_CREATOR_HEADERS} ${PROTO_SRCS} ${PROTO_HDRS})

target_compile_features(trancecreator PRIVATE cxx_std_17)

target_link_libraries(trancecreator ${Protobuf_LIBRARIES})
target_link_libraries(trancecreator ${wxWidgets_LIBRARIES})
target_link_libraries(trancecreator vpx)
target_link_libraries(trancecreator libwebm)

