cmake_minimum_required(VERSION 3.1.0)

##
## Try to find the Chrome libwebm library. Bit of a hack, as Ubuntu doesn't appear to package this.
##

FIND_PATH(LIBWEBM_INCLUDE_DIR
        NAMES mkvparser.hpp
        PATHS /usr/local/include/
        /usr/include/
        /home/srdjan/Development/libwebm_git/libwebm/
        )
MARK_AS_ADVANCED(LIBWEBM_INCLUDE_DIR)
FIND_LIBRARY(LIBWEBM_LIBRARY
        NAMES libwebm/libwebm.a
        PATHS ${LIBWEBM_INCLUDE_DIR}
        )
MARK_AS_ADVANCED(LIBWEBM_LIBRARY)

message("webm lib:     ${LIBWEBM_LIBRARY}")
message("webm include: ${LIBWEBM_INCLUDE_DIR}")

IF (LIBWEBM_INCLUDE_DIR AND LIBWEBM_LIBRARY)
    SET(WEBM_FOUND 1)
    SET(WEBM_LIBRARIES ${LIBWEBM_LIBRARY})
    SET(WEBM_INCLUDE_DIRS ${LIBWEBM_INCLUDE_DIR})
ENDIF()

MARK_AS_ADVANCED(WEBM_LIBRARIES)
MARK_AS_ADVANCED(WEBM_INCLUDE_DIRS)

IF (WEBM_FOUND)
    MESSAGE(STATUS "Found libwebm: ${WEBM_INCLUDE_DIRS}, ${WEBM_LIBRARIES}")
ELSE ()
    MESSAGE(STATUS "Could not find libwebm")
ENDIF()

add_library(libwebm UNKNOWN IMPORTED)
set_property(TARGET libwebm PROPERTY IMPORTED_LOCATION ${WEBM_LIBRARIES})
target_include_directories(libwebm INTERFACE ${WEBM_INCLUDE_DIRS})


