cmake_minimum_required(VERSION 3.25)
# --speech-report with --speech-levels auto: one row per changed line, numbered from 1 in the input (context
# lines not counted); keep (the default, any case) changes nothing, says why the report is empty and how to
# turn auto on.
if(NOT DEFINED CLI OR NOT DEFINED DICTS OR NOT DEFINED WORK)
    message(FATAL_ERROR "CLI, DICTS and WORK are required")
endif()
string(RANDOM LENGTH 12 ALPHABET abcdef0123456789 run)
set(root "${WORK}/${run}")
file(MAKE_DIRECTORY "${root}/home")
# The second line is spoken plainly; the word stage gives it -다, the speech levels 해체. The input has CRLF
# line ends (file(WRITE) would turn \n into CRLF on Windows only).
file(CONFIGURE OUTPUT "${root}/in.txt" CONTENT "「はい」\n「……子爵の娘は優秀だと聞いている」\n" NEWLINE_STYLE DOS)
file(CONFIGURE OUTPUT "${root}/context.txt" CONTENT "「……子爵の娘は優秀だと聞いている」\n「はい」\n" NEWLINE_STYLE UNIX)

function(run_cli report)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=HDTRANS_SPEECH_LEVELS "HOME=${root}/home"
        "HDTRANS_CACHE_DIR=off" "${CLI}" --dicts "${DICTS}" --userdict "" --in "${root}/in.txt"
        --speech-report "${root}/${report}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error ENCODING UTF-8)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "CLI failed (${status}): ${error}")
    endif()
    file(READ "${root}/${report}" text)
    set(output "${output}" PARENT_SCOPE)
    set(error "${error}" PARENT_SCOPE)
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(require_row label)
    string(REGEX MATCHALL "[^\n]*\n" rows "${text}")
    list(LENGTH rows count)
    if(NOT count EQUAL 1 OR NOT text MATCHES "^2\tREG-[0-9]+\t[^\t\n]*\t「……子爵の娘は優秀だと聞いている」\t[^\t\r\n]+\t[^\t\r\n]+\n$")
        message(FATAL_ERROR "${label}: expected one row for line 2: ${text}")
    endif()
endfunction()

run_cli(auto.tsv --speech-levels auto)
require_row("document")
string(REGEX MATCH "^[^\t]*\t[^\t]*\t[^\t]*\t[^\t]*\t([^\t]*)\t([^\n]*)\n" row "${text}")
set(before "${CMAKE_MATCH_1}")
set(after "${CMAKE_MATCH_2}")
if(before STREQUAL after OR NOT output MATCHES "${after}")
    message(FATAL_ERROR "the row must hold the old and the new text, the new as output: ${text} / ${output}")
endif()
run_cli(context.tsv --speech-levels auto --context "${root}/context.txt")
require_row("with context")
run_cli(each.tsv --speech-levels auto --each)
require_row("each line")

foreach(keep IN ITEMS default Keep)
    if(keep STREQUAL "default")
        run_cli(keep-default.tsv)
    else()
        run_cli(keep.tsv --speech-levels ${keep})
    endif()
    if(NOT text STREQUAL "" OR NOT error MATCHES "speech report is empty" OR NOT error MATCHES "--speech-levels auto"
            OR NOT error MATCHES "speech_levels: auto" OR output MATCHES "${after}")
        message(FATAL_ERROR "keep (${keep}) must change nothing and say how to turn auto on: ${text} / ${error}")
    endif()
endforeach()
run_cli(reference.tsv --speech-levels auto --reference)
if(NOT text STREQUAL "" OR NOT error MATCHES "--reference")
    message(FATAL_ERROR "--reference must change nothing and say so: ${text} / ${error}")
endif()
