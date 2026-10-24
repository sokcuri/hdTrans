# Sets HDTRANS_VERSION and HDTRANS_COMMIT for the source tree in dir. A Git checkout exactly on a
# v<version> tag gives that version and no commit; any other checkout gives "dev" and the short
# commit hash. Without Git the version comes from release-version.txt, else "dev".
function(hdtrans_detect_version dir)
    set(version "")
    set(commit "")
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match --match "v[0-9]*"
            WORKING_DIRECTORY "${dir}"
            OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT version)
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                WORKING_DIRECTORY "${dir}"
                OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(commit)
                set(version "dev")
            endif()
        endif()
    endif()
    if(NOT version AND EXISTS "${dir}/release-version.txt")
        file(STRINGS "${dir}/release-version.txt" version LIMIT_COUNT 1)
    endif()
    if(NOT version)
        set(version "dev")
    endif()
    string(REGEX REPLACE "^v([0-9])" "\\1" version "${version}")
    set(HDTRANS_VERSION "${version}" PARENT_SCOPE)
    set(HDTRANS_COMMIT "${commit}" PARENT_SCOPE)
endfunction()
