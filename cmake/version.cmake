find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE HDTRANS_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT HDTRANS_VERSION)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE shortHash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
endif()
if(NOT HDTRANS_VERSION)
    if(shortHash)
        set(HDTRANS_VERSION "0.0.0-${shortHash}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/release-version.txt")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/release-version.txt" HDTRANS_VERSION LIMIT_COUNT 1)
    else()
        set(HDTRANS_VERSION "0.0.0-unknown")
    endif()
endif()
string(REGEX REPLACE "^v([0-9])" "\\1" HDTRANS_VERSION "${HDTRANS_VERSION}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hdtrans-version.txt" "${HDTRANS_VERSION}\n")
message(STATUS "hdTrans version: ${HDTRANS_VERSION}")

set(HDTRANS_VERSION_NUMERIC "0,0,0,0")
if(HDTRANS_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(HDTRANS_VERSION_NUMERIC "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
endif()

function(hdtrans_version_resource target filename filetype)
    set(HDTRANS_RESOURCE_FILENAME "${filename}")
    set(HDTRANS_RESOURCE_TYPE "${filetype}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}_version.rc")
endfunction()
