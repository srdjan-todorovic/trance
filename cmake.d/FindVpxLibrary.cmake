cmake_minimum_required(VERSION 3.1.0)

##
## Try to find the VPX library from webm, /usr/lib/x86_64-linux-gnu/pkgconfig/vpx.pc
##

FIND_PATH(VPX_INCLUDE_DIR
        NAMES vpx/vpx_encoder.h
        PATHS /usr/local/include/
        /usr/include/)
MARK_AS_ADVANCED(VPX_INCLUDE_DIR)
FIND_LIBRARY(VPX_LIBRARY
        NAMES vpx
        PATHS ${LIBVPXDIR}/lib/
        /usr/lib/arm-linux-gnueabihf/
        /usr/lib/arm-linux-gnueabi/
        /usr/lib/x86_64-linux-gnu/
        /usr/local/lib64/
        /usr/lib64/
        /usr/lib/)
MARK_AS_ADVANCED(VPX_LIBRARY)

IF (VPX_INCLUDE_DIR AND VPX_LIBRARY)
    SET(VPX_FOUND 1)
    SET(VPX_LIBRARIES ${VPX_LIBRARY})
    SET(VPX_INCLUDE_DIRS ${VPX_INCLUDE_DIR})
ENDIF()

MARK_AS_ADVANCED(VPX_LIBRARIES)
MARK_AS_ADVANCED(VPX_INCLUDE_DIRS)

IF (VPX_FOUND)
    MESSAGE(STATUS "Found libvpx: ${VPX_INCLUDE_DIRS}, ${VPX_LIBRARIES}")
ELSE ()
    MESSAGE(STATUS "Could not find libvpx")
ENDIF()

add_library(vpx UNKNOWN IMPORTED)
set_property(TARGET vpx PROPERTY IMPORTED_LOCATION ${VPX_LIBRARIES})
target_include_directories(vpx INTERFACE ${VPX_INCLUDE_DIRS})


