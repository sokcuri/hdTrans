# Writes OUT, a C++ source holding text files as arrays with one string literal per line.
#   cmake -DDIR=<dir> -DOUT=<file> -DTABLES=<array> -DACCESSOR=<function> -P builtin_tables.cmake
#     the *.tsv files of DIR in name order as BuiltinTable arrays, and the function ACCESSOR
#     that returns them
#   cmake -DFILE=<file> -DOUT=<file> -DHEADER=<include> -DNAMESPACE=<namespace> -DACCESSOR=<function>
#         -P builtin_tables.cmake
#     the lines of FILE, each ending in "\n" but a missing last one, and the function ACCESSOR
#     (declared in HEADER) that returns them as std::span<const std::string_view>
#   cmake -DCLASSES=<file> -DOUT=<file> -P builtin_tables.cmake
#     the names of the sets of the word-class table FILE (its "//= NAME" lines) as kClassNames,
#     written only when they change, so that an edit of the members compiles nothing
#   cmake -DRULE_DIR=<dir> -DOUT=<file> -P builtin_tables.cmake
#     the rules program: the header "rules 1 LOCALCOUNT GLOBALCOUNT" and the lines of the *.txt
#     files of RULE_DIR in name order, as RuleLines(), and each file's name and lines as
#     RuleFiles() (core/engine/rules.h)

# Sets VAR to the initializer of a std::string_view array holding the lines of the file at path,
# as ""sv literals: GCC counts the length of a plain literal against its constexpr limit.
function(hdtrans_literal_lines path var)
    file(READ "${path}" text)
    if(text STREQUAL "")
        message(FATAL_ERROR "${path} is empty")
    endif()
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE "\"" "\\\"" text "${text}")
    string(REPLACE "\t" "\\t" text "${text}")
    string(REPLACE "\r" "\\r" text "${text}")
    set(end "\"sv,\n")
    string(LENGTH "${text}" size)
    math(EXPR last "${size} - 1")
    string(SUBSTRING "${text}" ${last} 1 tail)
    if(tail STREQUAL "\n")
        string(SUBSTRING "${text}" 0 ${last} text)
        set(end "\\n\"sv,\n")
    endif()
    string(REPLACE "\n" "\\n\"sv,\n    \"" text "${text}")
    set(${var} "{\n    \"${text}${end}}" PARENT_SCOPE)
endfunction()

if(DEFINED CLASSES)
    file(READ "${CLASSES}" text)
    string(REPLACE "\r" "" text "\n${text}")
    string(REGEX MATCHALL "\n//= [a-z][a-z0-9.-]*\t" names "${text}")
    set(body "")
    foreach(name IN LISTS names)
        string(REGEX REPLACE "\n//= ([^\t]*)\t" "\\1" name "${name}")
        string(APPEND body "    \"${name}\",\n")
    endforeach()
    string(CONCAT header "// Generated table; do not edit.\n#pragma once\n#include <string_view>\n\n"
        "namespace hdTrans::dict::internal {\n\n// The names of the sets of classes.tsv.\n"
        "inline constexpr std::string_view kClassNames[] = {\n${body}};\n\n}  // namespace hdTrans::dict::internal\n")
    set(old "")
    if(EXISTS "${OUT}")
        file(READ "${OUT}" old)
    endif()
    if(NOT old STREQUAL header)
        file(WRITE "${OUT}" "${header}")
    endif()
    return()
endif()

if(DEFINED RULE_DIR)
    file(GLOB files LIST_DIRECTORIES false "${RULE_DIR}/*.txt")
    list(SORT files)
    set(body "")
    set(table "")
    set(locals 0)
    set(globals 0)
    set(first 1)
    foreach(path IN LISTS files)
        file(READ "${path}" text)
        string(REGEX MATCHALL "\nL [0-9]+ " l "\n${text}")
        string(REGEX MATCHALL "\nG -?[0-9]+ " g "\n${text}")
        list(LENGTH l nl)
        list(LENGTH g ng)
        math(EXPR locals "${locals} + ${nl}")
        math(EXPR globals "${globals} + ${ng}")
        string(REGEX MATCHALL "\n" breaks "${text}")
        list(LENGTH breaks count)
        if(NOT text MATCHES "\n$")
            message(FATAL_ERROR "${path} does not end in a line break")
        endif()
        hdtrans_literal_lines("${path}" lines)
        string(LENGTH "${lines}" size)
        math(EXPR size "${size} - 3")
        string(SUBSTRING "${lines}" 2 ${size} lines)
        string(APPEND body "${lines}")
        get_filename_component(name "${path}" NAME)
        string(APPEND table "    {\"${name}\", ${first}, ${count}},\n")
        math(EXPR first "${first} + ${count}")
    endforeach()
    file(WRITE "${OUT}" "// Generated table; do not edit.\n#include <span>\n#include <string_view>\n\n"
        "#include \"core/engine/rules.h\"\n\nnamespace hdTrans::internal {\n\nnamespace {\n\n"
        "using namespace std::string_view_literals;\n\n"
        "constexpr std::string_view kLines[] = {\n    \"rules 1 ${locals} ${globals}\\n\"sv,\n${body}};\n\n"
        "constexpr RuleFile kFiles[] = {\n${table}};\n\n}  // namespace\n\n"
        "std::span<const std::string_view> RuleLines() { return kLines; }\n\n"
        "std::span<const RuleFile> RuleFiles() { return kFiles; }\n\n}  // namespace hdTrans::internal\n")
    return()
endif()

if(DEFINED FILE)
    hdtrans_literal_lines("${FILE}" lines)
    file(WRITE "${OUT}" "// Generated table; do not edit.\n#include <span>\n#include <string_view>\n\n"
        "#include \"${HEADER}\"\n\nnamespace ${NAMESPACE} {\n\nnamespace {\n\nusing namespace std::string_view_literals;\n\n"
        "constexpr std::string_view kLines[] = ${lines};\n\n}  // namespace\n\n"
        "std::span<const std::string_view> ${ACCESSOR}() { return kLines; }\n\n}  // namespace ${NAMESPACE}\n")
    return()
endif()

file(GLOB files LIST_DIRECTORIES false "${DIR}/*.tsv")
set(out "// Generated table; do not edit.\n#include \"core/dict/builtin.h\"\n\nnamespace hdTrans::dict::internal {\n\nnamespace {\n\nusing namespace std::string_view_literals;\n\n")
set(entries "")
foreach(path IN LISTS files)
    get_filename_component(name "${path}" NAME)
    get_filename_component(base "${path}" NAME_WE)
    string(SUBSTRING "${base}" 0 1 first)
    string(SUBSTRING "${base}" 1 -1 rest)
    string(TOUPPER "${first}" first)
    set(symbol "k${first}${rest}")
    hdtrans_literal_lines("${path}" lines)
    string(APPEND out "constexpr std::string_view ${symbol}[] = ${lines};\n\n")
    string(APPEND entries "    {\"${name}\", ${symbol}},\n")
endforeach()
string(APPEND out "constexpr BuiltinTable ${TABLES}[] = {\n${entries}};\n\n}  // namespace\n\n"
    "std::span<const BuiltinTable> ${ACCESSOR}() { return ${TABLES}; }\n\n}  // namespace hdTrans::dict::internal\n")
file(WRITE "${OUT}" "${out}")
