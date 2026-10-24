include("${CMAKE_CURRENT_LIST_DIR}/detect_version.cmake")
hdtrans_detect_version("${CMAKE_CURRENT_SOURCE_DIR}")
# HDTRANS_VERSION is the engine version of the APIs; the CLI and the Windows version strings show
# the commit after it, and hdtrans-version.txt names the release packages.
set(HDTRANS_VERSION_TEXT "${HDTRANS_VERSION}")
set(packageVersion "${HDTRANS_VERSION}")
if(HDTRANS_COMMIT)
    set(HDTRANS_VERSION_TEXT "${HDTRANS_VERSION} (${HDTRANS_COMMIT})")
    set(packageVersion "${HDTRANS_VERSION}-${HDTRANS_COMMIT}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hdtrans-version.txt" "${packageVersion}\n")
message(STATUS "hdTrans version: ${HDTRANS_VERSION_TEXT}")

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
