# Static/shared engine libraries, CLI and unit tests on any C++20 compiler;
# J2KEngine.dll (J2K_* exports) on Windows; WebAssembly with Emscripten. Presets: CMakePresets.json.
cmake_minimum_required(VERSION 3.25)
project(hdtrans LANGUAGES C CXX)
include(cmake/version.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(HDTRANS_WERROR "Treat compiler warnings as errors" ON)
option(HDTRANS_INTERPRET_RULES "Evaluate every selection rule with the interpreter, ignoring compiled rules" OFF)
option(HDTRANS_FUZZ "Build the libFuzzer target hdtrans_fuzz and instrument every target for it (Clang)" OFF)
option(HDTRANS_PASS_EXPERIMENTS "Read HDTRANS_PASS_SKIP and HDTRANS_PASS_SHUFFLE to leave out or reorder sentence passes (measurement builds only)" OFF)

if(MSVC)
    # Static CRT: the DLL is one file inside a host that ships no runtime of ours.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Replace CMake's defaults so the command line holds only what we ask for.
    set(CMAKE_CXX_FLAGS "/EHs-c- /GR- /utf-8 /permissive- /W4 /Zc:inline /diagnostics:column")
    set(CMAKE_C_FLAGS "/utf-8 /W4 /diagnostics:column")
    if(HDTRANS_WERROR)
        string(APPEND CMAKE_CXX_FLAGS " /WX")
        string(APPEND CMAKE_C_FLAGS " /WX")
    endif()
    if(CMAKE_GENERATOR MATCHES "^Visual Studio ")
        string(APPEND CMAKE_CXX_FLAGS " /MP")
        string(APPEND CMAKE_C_FLAGS " /MP")
    endif()
    set(CMAKE_C_FLAGS_RELEASE "/O2 /Oi /Gy /DNDEBUG")
    set(CMAKE_C_FLAGS_DEBUG "/Od /RTC1 /Zi /D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Oi /Gy /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /RTC1 /Zi /D_DEBUG")
    add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _HAS_EXCEPTIONS=0)
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /OPT:REF /OPT:ICF /DEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /OPT:REF /OPT:ICF /DEBUG")
else()
    add_compile_options(-Wall -Wextra -fno-exceptions -fno-rtti)
    # A local that hides a local or a parameter (MSVC C4456, C4457). GCC's -Wshadow=local counts the
    # enclosing function's locals inside a lambda too, which Clang reports with -Wshadow-uncaptured-local;
    # Clang's -Wshadow also reports hidden members and globals (C4458, C4459).
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wshadow> $<$<COMPILE_LANGUAGE:CXX>:-Wshadow-uncaptured-local>)
    else()
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wshadow=local>)
    endif()
    if(HDTRANS_WERROR)
        add_compile_options(-Werror)
    endif()
    # Sanitizers for every target, e.g. address,undefined. Any report aborts the program.
    set(HDTRANS_SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list (GCC/Clang)")
    if(HDTRANS_SANITIZE)
        add_compile_options(-fsanitize=${HDTRANS_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
        if(HDTRANS_SANITIZE MATCHES "undefined")
            # vptr needs RTTI, which the build turns off.
            add_compile_options(-fno-sanitize=vptr)
        endif()
        add_link_options(-fsanitize=${HDTRANS_SANITIZE})
    endif()
    if(HDTRANS_FUZZ)
        # The sanitizer runtimes also define the coverage hooks for the targets that do not link libFuzzer.
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT HDTRANS_SANITIZE)
            message(FATAL_ERROR "HDTRANS_FUZZ needs Clang, its libFuzzer and HDTRANS_SANITIZE")
        endif()
        add_compile_options(-fsanitize=fuzzer-no-link)
    endif()
endif()

# The builtin dictionary tables, src/core/dict/builtin/*.tsv and builtin/extra/*.tsv, compiled in.
set(HDTRANS_BUILTIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core/dict/builtin")
file(GLOB HDTRANS_BUILTIN_TSV CONFIGURE_DEPENDS "${HDTRANS_BUILTIN_DIR}/*.tsv")
file(GLOB HDTRANS_BUILTIN_EXTRA_TSV CONFIGURE_DEPENDS "${HDTRANS_BUILTIN_DIR}/extra/*.tsv")
# A stamp lists the table files; it changes only when a table is added or removed, so that a
# removed table also regenerates the source.
function(hdtrans_builtin_source output dir array accessor inputs)
    set(script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_tables.cmake")
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${output}.tables")
    string(REPLACE ";" "\n" names "${${inputs}}")
    file(CONFIGURE OUTPUT "${stamp}" CONTENT "${names}\n" @ONLY)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${output}"
        COMMAND "${CMAKE_COMMAND}" "-DDIR=${dir}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/${output}"
            "-DTABLES=${array}" "-DACCESSOR=${accessor}" -P "${script}"
        DEPENDS ${${inputs}} "${stamp}" "${script}" VERBATIM)
endfunction()
hdtrans_builtin_source(builtin_gen.cpp "${HDTRANS_BUILTIN_DIR}" kTables BuiltinTables HDTRANS_BUILTIN_TSV)
hdtrans_builtin_source(builtin_extra_gen.cpp "${HDTRANS_BUILTIN_DIR}/extra" kFiles ExtraBuiltinFiles
    HDTRANS_BUILTIN_EXTRA_TSV)
# The tables of the speech-level stage, src/core/register/data/*.tsv and its folders, compiled in.
set(HDTRANS_REGISTER_DATA "${CMAKE_CURRENT_SOURCE_DIR}/src/core/register/data")
file(GLOB HDTRANS_REGISTER_READER_TSV CONFIGURE_DEPENDS "${HDTRANS_REGISTER_DATA}/*.tsv")
file(GLOB HDTRANS_REGISTER_KO_TSV CONFIGURE_DEPENDS "${HDTRANS_REGISTER_DATA}/ko/*.tsv")
file(GLOB HDTRANS_REGISTER_LEVEL_TSV CONFIGURE_DEPENDS "${HDTRANS_REGISTER_DATA}/register/*.tsv")
file(GLOB HDTRANS_REGISTER_RETRO_TSV CONFIGURE_DEPENDS "${HDTRANS_REGISTER_DATA}/retro/*.tsv")
hdtrans_builtin_source(register_reader_gen.cpp "${HDTRANS_REGISTER_DATA}" kReaderFiles RegisterReaderTables
    HDTRANS_REGISTER_READER_TSV)
hdtrans_builtin_source(register_ko_gen.cpp "${HDTRANS_REGISTER_DATA}/ko" kKoFiles RegisterKoTables HDTRANS_REGISTER_KO_TSV)
hdtrans_builtin_source(register_levels_gen.cpp "${HDTRANS_REGISTER_DATA}/register" kLevelFiles RegisterLevelTables
    HDTRANS_REGISTER_LEVEL_TSV)
hdtrans_builtin_source(register_retro_gen.cpp "${HDTRANS_REGISTER_DATA}/retro" kRetroFiles RegisterRetroTables
    HDTRANS_REGISTER_RETRO_TSV)
set(HDTRANS_REGISTER_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/register_reader_gen.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/register_ko_gen.cpp" "${CMAKE_CURRENT_BINARY_DIR}/register_levels_gen.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/register_retro_gen.cpp")
# The set names of the word classes, checked when a source names a class.
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/classes_gen.h"
    COMMAND "${CMAKE_COMMAND}" "-DCLASSES=${HDTRANS_BUILTIN_DIR}/classes.tsv"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/classes_gen.h" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_tables.cmake"
    DEPENDS "${HDTRANS_BUILTIN_DIR}/classes.tsv" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_tables.cmake" VERBATIM)
# The rules program of the word stage, the *.txt files of src/core/rules, compiled in; a stamp
# lists the files so that adding or removing one regenerates the source.
set(HDTRANS_RULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core/rules")
file(GLOB HDTRANS_RULES CONFIGURE_DEPENDS "${HDTRANS_RULE_DIR}/*.txt")
list(SORT HDTRANS_RULES)
string(REPLACE ";" "\n" HDTRANS_RULE_NAMES "${HDTRANS_RULES}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp.files" CONTENT "${HDTRANS_RULE_NAMES}\n" @ONLY)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp"
    COMMAND "${CMAKE_COMMAND}" "-DRULE_DIR=${HDTRANS_RULE_DIR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_tables.cmake"
    DEPENDS ${HDTRANS_RULES} "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp.files"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_tables.cmake" VERBATIM)

add_subdirectory(third_party/pcre2)
add_subdirectory(third_party/libyaml)

file(GLOB_RECURSE HDTRANS_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(hdtrans STATIC ${HDTRANS_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/builtin_gen.cpp" "${CMAKE_CURRENT_BINARY_DIR}/builtin_extra_gen.cpp"
    ${HDTRANS_REGISTER_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp" "${CMAKE_CURRENT_BINARY_DIR}/classes_gen.h")
target_link_libraries(hdtrans PRIVATE pcre2-16 yaml)
set_source_files_properties(src/cli/main.cpp PROPERTIES
    COMPILE_DEFINITIONS HDTRANS_VERSION_TEXT="${HDTRANS_VERSION_TEXT}")
set_source_files_properties(src/cshared/hdtrans_c.cpp PROPERTIES
    COMPILE_DEFINITIONS HDTRANS_VERSION="${HDTRANS_VERSION}")
if(HDTRANS_INTERPRET_RULES)
    target_compile_definitions(hdtrans PRIVATE HDTRANS_INTERPRET_RULES)
endif()
if(HDTRANS_PASS_EXPERIMENTS)
    target_compile_definitions(hdtrans PRIVATE HDTRANS_PASS_EXPERIMENTS)
endif()
file(GLOB_RECURSE HDTRANS_DATA_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
list(APPEND HDTRANS_DATA_SOURCES ${HDTRANS_RULES})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HDTRANS_DATA_SOURCES})
set(HDTRANS_DATA_HASHES "")
foreach(dataFile IN LISTS HDTRANS_DATA_SOURCES)
    file(SHA256 "${dataFile}" dataHash)
    file(RELATIVE_PATH dataName "${CMAKE_CURRENT_SOURCE_DIR}" "${dataFile}")
    string(APPEND HDTRANS_DATA_HASHES "${dataName}:${dataHash}\n")
endforeach()
string(SHA256 HDTRANS_DATA_ID "${HDTRANS_DATA_HASHES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HDTRANS_BUILTIN_TSV} ${HDTRANS_BUILTIN_EXTRA_TSV})
set(HDTRANS_BUILTIN_HASHES "")
foreach(tableFile IN LISTS HDTRANS_BUILTIN_TSV HDTRANS_BUILTIN_EXTRA_TSV)
    file(SHA256 "${tableFile}" tableHash)
    file(RELATIVE_PATH tableName "${HDTRANS_BUILTIN_DIR}" "${tableFile}")
    string(APPEND HDTRANS_BUILTIN_HASHES "${tableName}:${tableHash}\n")
endforeach()
string(SHA256 HDTRANS_BUILTIN_ID "${HDTRANS_BUILTIN_HASHES}")
string(SHA256 HDTRANS_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}:${CMAKE_CXX_COMPILER_VERSION}:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}:${CMAKE_SIZEOF_VOID_P}:${CMAKE_BUILD_TYPE}:${CMAKE_CXX_FLAGS}:${CMAKE_CXX_FLAGS_RELEASE}:${CMAKE_CXX_FLAGS_DEBUG}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dictionary_ids.h" CONTENT
"#pragma once\n#define HDTRANS_DATA_ID \"${HDTRANS_DATA_ID}\"\n#define HDTRANS_BUILTIN_ID \"${HDTRANS_BUILTIN_ID}\"\n#define HDTRANS_COMPILER_ID \"${HDTRANS_COMPILER_ID}\"\n")
set_target_properties(hdtrans PROPERTIES OUTPUT_NAME hdTransCore COMPILE_PDB_NAME hdTransCore)
target_include_directories(hdtrans PUBLIC include PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")

if(NOT EMSCRIPTEN)
    set_target_properties(hdtrans PROPERTIES POSITION_INDEPENDENT_CODE ON)
    find_package(Threads REQUIRED)
    target_link_libraries(hdtrans PUBLIC Threads::Threads)

    add_executable(hdtrans_cli src/cli/main.cpp src/frontend/dictionary_version.cpp)
    target_include_directories(hdtrans_cli PRIVATE src)
    target_link_libraries(hdtrans_cli PRIVATE hdtrans)
    set_target_properties(hdtrans_cli PROPERTIES OUTPUT_NAME hdtrans PDB_NAME hdtrans_cli)
    set(HDTRANS_USER_STAGE "$<TARGET_FILE_DIR:hdtrans_cli>/hdtrans-data/user")
    add_custom_command(TARGET hdtrans_cli POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HDTRANS_USER_STAGE}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/user/userdict.example.txt"
            "${HDTRANS_USER_STAGE}/userdict.example.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/hdtrans.config.example.yaml"
            "${HDTRANS_USER_STAGE}/hdtrans.config.example.yaml"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ehnd"
            "$<TARGET_FILE_DIR:hdtrans_cli>/hdtrans-data/ehnd" VERBATIM)


    file(GLOB_RECURSE HDTRANS_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
    add_library(hdtrans_gui_support STATIC src/gui/text_file.cpp src/gui/translation.cpp src/gui/user_dicts.cpp)
    target_include_directories(hdtrans_gui_support PUBLIC src)
    target_link_libraries(hdtrans_gui_support PUBLIC hdtrans)
    if(NOT WIN32)
        find_package(Iconv REQUIRED)
        target_link_libraries(hdtrans_gui_support PRIVATE Iconv::Iconv)
    endif()

    add_executable(hdtrans_tests ${HDTRANS_TEST_SOURCES} src/frontend/dictionary_version.cpp)
    target_include_directories(hdtrans_tests PRIVATE tests/unit src "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(hdtrans_tests PRIVATE hdtrans_gui_support)
    enable_testing()
    add_test(NAME cli.version COMMAND hdtrans_cli --version --dicts "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/dict/testdata/folder")
    add_test(NAME cli.options COMMAND "${CMAKE_COMMAND}" -DCLI=$<TARGET_FILE:hdtrans_cli>
        -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/dict/testdata/folder
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-options -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_options.cmake")
    string(REGEX REPLACE "([][+.*()^$?|\\])" "\\\\\\1" versionPattern "${HDTRANS_VERSION_TEXT}")
    # The speech report of the CLI, with the dictionaries of dicts/.
    add_test(NAME cli.speech-report COMMAND "${CMAKE_COMMAND}" -DCLI=$<TARGET_FILE:hdtrans_cli>
        -DDICTS=${CMAKE_CURRENT_SOURCE_DIR}/dicts -DWORK=${CMAKE_CURRENT_BINARY_DIR}/speech-report
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/speech_report.cmake")
    set_tests_properties(cli.speech-report PROPERTIES TIMEOUT 300)
    set_tests_properties(cli.version PROPERTIES
        PASS_REGULAR_EXPRESSION "^hdTrans v${versionPattern} \\(native\\) / dict "
        ENVIRONMENT "HDTRANS_CACHE_DIR=off")
    add_test(NAME version COMMAND "${CMAKE_COMMAND}" -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/version-test -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/version.cmake")
    # One ctest entry per line of tests/unit/shards.txt, and a check that every test has a shard.
    set(unitShards "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/shards.txt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${unitShards}")
    file(STRINGS "${unitShards}" unitShardLines REGEX "^[^#]")
    add_test(NAME unit.shards COMMAND hdtrans_tests --check-shards "${unitShards}")
    foreach(line IN LISTS unitShardLines)
        string(REGEX MATCH "^[^ \t]+" shard "${line}")
        add_test(NAME unit.${shard} COMMAND hdtrans_tests --shard "${unitShards}" ${shard})
        set_tests_properties(unit.${shard} PROPERTIES ENVIRONMENT
            "HDTRANS_DICTS=${CMAKE_CURRENT_SOURCE_DIR}/dicts;HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/unit-cache/${shard}")
    endforeach()
    add_test(NAME dicts.classes COMMAND "${CMAKE_COMMAND}" -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/word_classes.cmake")
    # The shipped rows pass --verify-dicts: every marker family expands.
    add_test(NAME dicts.verify COMMAND hdtrans_cli --dicts "${CMAKE_CURRENT_SOURCE_DIR}/dicts" --no-cache --verify-dicts
        -in "${CMAKE_CURRENT_SOURCE_DIR}/tests/empty.in")
    if(UNIX)
        add_test(NAME cli.config COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/config.sh" "$<TARGET_FILE:hdtrans_cli>")
        add_test(NAME cli.why COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/why.sh" "$<TARGET_FILE:hdtrans_cli>")
        add_test(NAME sound.long-line COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/long_line.sh" "$<TARGET_FILE:hdtrans_cli>"
            "${CMAKE_CURRENT_SOURCE_DIR}/dicts")
        set_tests_properties(sound.long-line PROPERTIES TIMEOUT 60)
        add_test(NAME tools.selfbase COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/selfbase.sh" "$<TARGET_FILE:hdtrans_cli>")
        add_test(NAME dicts.ids COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/dict_ids.sh" "${CMAKE_CURRENT_SOURCE_DIR}/dicts")
        find_program(HDTRANS_PYTHON python3)
        if(HDTRANS_PYTHON)
            foreach(tool union-tsv accept-cases chrf rule-ids verdicts count-class hygiene pass-mutation
                    construction-cases audit blind new-pairs perf-budget perf-base fuzz variants nn-rows
                    affected sets judge-packet)
                add_test(NAME tools.${tool} COMMAND "${HDTRANS_PYTHON}" -I
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/${tool}.py" --self-test)
            endforeach()
            add_test(NAME tools.package-release COMMAND "${HDTRANS_PYTHON}" -I
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_package_release.py")
            add_test(NAME tools.pick COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/pick.sh")
            add_test(NAME tools.measure COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/measure.sh"
                "$<TARGET_FILE:hdtrans_cli>")
            add_test(NAME tools.check-fix COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/check-fix.sh"
                "$<TARGET_FILE:hdtrans_cli>")
            # Every noun row that does not read as a bare noun has gram Nn or an allowance (tools/nn-allowance.tsv).
            add_test(NAME dicts.nn-rows COMMAND "${HDTRANS_PYTHON}" -I
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/nn-rows.py" --check)
            # The name heads table (builtin/extra/name_heads.tsv) is the one the name dictionaries give.
            add_test(NAME dicts.name-heads COMMAND "${HDTRANS_PYTHON}" -I
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/name-heads.py" --check "${CMAKE_CURRENT_SOURCE_DIR}/dicts")
            # Every row of the construction table has a fix and a keep rule case.
            add_test(NAME rules.constructions COMMAND "${HDTRANS_PYTHON}" -I
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/construction-cases.py" --check)
        endif()
    endif()

    # Translates examples/ with the dictionaries in HDTRANS_DICTS at test time; skipped when unset.
    find_program(HDTRANS_SH sh)
    if(HDTRANS_SH)
        add_test(NAME examples COMMAND "${HDTRANS_SH}" -c "exec \"$0\" \"$1\" \"$2\" \"$HDTRANS_DICTS\""
            "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/examples.sh" $<TARGET_FILE:hdtrans_cli>)
        set_tests_properties(examples PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300
            ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/examples-cache")
        # One table per theme, tests/rules/cases/<theme>.tsv.
        add_test(NAME rules.cases.themes COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
            $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/cases")
        # Meaning flips, tests/rules/critical/<kind>.tsv: every row passes, none is current.
        add_test(NAME rules.critical COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
            $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/critical"
            --no-current)
        # Each speech level: tests/rules/speech-<level>/<theme>.tsv (strict is the default).
        foreach(level soft strict)
            add_test(NAME rules.cases.speech-${level} COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
                $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/speech-${level}" -- --speech ${level})
        endforeach()
        # Wrapped lines, each input a document of its own: tests/rules/wrapped/ and, at the soft
        # speech level, tests/rules/wrapped-soft/.
        add_test(NAME rules.cases.wrapped COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
            $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/wrapped"
            --documents -- --wrapped)
        add_test(NAME rules.cases.wrapped-soft COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
            $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/wrapped-soft"
            --documents -- --wrapped --speech soft)
        # Documents, each input a document of its own: tests/rules/documents/.
        add_test(NAME rules.cases.documents COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
            $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/documents"
            --documents)
        # The sample pack loaded after the dictionaries (docs/packs.md): its fix and keep cases.
        add_test(NAME rules.packs COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
            $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts,${CMAKE_CURRENT_SOURCE_DIR}/tests/packs/sample"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/packs/sample.cases.tsv" --no-current)
        set_tests_properties(rules.critical rules.cases.themes rules.cases.speech-soft rules.cases.speech-strict
            rules.cases.wrapped rules.cases.wrapped-soft rules.cases.documents rules.packs
            PROPERTIES TIMEOUT 300 ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/examples-cache")
        # No input of the rule cases or of tests/rules/drops/ loses a word, but for the known gaps
        # (where python3 is found, as for the tools.* tests).
        if(HDTRANS_PYTHON)
            add_test(NAME rules.drops COMMAND "${HDTRANS_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/drops.py"
                $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/critical"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/cases" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/speech-soft"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/speech-strict" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/drops"
                --known "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/drops/known-gaps.txt")
            set_tests_properties(rules.drops PROPERTIES TIMEOUT 300)
            # Each family of tests/variants/ passes at least its minimum rate.
            add_test(NAME rules.variants COMMAND "${HDTRANS_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/variants.py"
                $<TARGET_FILE:hdtrans_cli> -d "${CMAKE_CURRENT_SOURCE_DIR}/dicts" --check)
            set_tests_properties(rules.variants PROPERTIES TIMEOUT 300)
            add_test(NAME tools.drops COMMAND "${HDTRANS_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/drops.py" --self-test)
            # The inputs of the measured sets that drop text or keep unplaced kana stay within
            # tools/drops-budget.tsv; skipped when no set variable is set.
            add_test(NAME sets.drops COMMAND "${HDTRANS_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/drops.py"
                $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts" --sets
                --budget "${CMAKE_CURRENT_SOURCE_DIR}/tools/drops-budget.tsv")
            set_tests_properties(sets.drops PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
        endif()
        # Compares with the expected outputs in HDTRANS_TESTDATA at test time; skipped when unset.
        add_test(NAME testdata COMMAND "${HDTRANS_SH}" -c "exec \"$0\" \"$1\" --expected \"$HDTRANS_TESTDATA\" \"$2\" \"$3\""
            "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e.sh" $<TARGET_FILE:hdtrans_cli>
            "${CMAKE_CURRENT_SOURCE_DIR}/dicts")
        set_tests_properties(testdata PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
        # Checks the unit test runner itself; it compiles a probe with the C++ compiler.
        if(NOT WIN32)
            add_test(NAME tests.runner COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/runner.sh")
            set_tests_properties(tests.runner PROPERTIES TIMEOUT 300
                ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}")
        endif()
    endif()

    add_executable(config_host tests/host/config.cpp src/cshared/hdtrans_c.cpp src/frontend/module_dir.cpp)
    target_include_directories(config_host PRIVATE src)
    target_link_libraries(config_host PRIVATE hdtrans ${CMAKE_DL_LIBS})
    set_target_properties(config_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/config-host")
    if(WIN32)
        add_test(NAME host.config COMMAND config_host "$<TARGET_FILE:j2kengine>")
    else()
        add_test(NAME host.config COMMAND config_host)
    endif()
    set_tests_properties(host.config PROPERTIES ENVIRONMENT "HDTRANS_CACHE_DIR=off")

    add_executable(admin_priority tests/host/admin_priority.cpp src/cshared/hdtrans_c.cpp src/frontend/module_dir.cpp)
    target_include_directories(admin_priority PRIVATE src)
    target_link_libraries(admin_priority PRIVATE hdtrans ${CMAKE_DL_LIBS})
    add_test(NAME host.admin_priority COMMAND admin_priority "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/dict/testdata/folder")
    set_tests_properties(host.admin_priority PROPERTIES TIMEOUT 60 PROCESSORS 8
        ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/admin-priority-cache")

    # The rule compiler (docs/rule-compiler.md), a host tool. The checks run on the shipped rules,
    # and the committed compiled rules must be its output for them.
    if(NOT CMAKE_CROSSCOMPILING)
        # It builds from the rule and code sources only, not from the library that holds its output.
        file(GLOB HDTRANS_RULEC_SOURCES CONFIGURE_DEPENDS src/rulec/*.cpp)
        add_executable(hdrulec ${HDTRANS_RULEC_SOURCES} src/core/rules/body.cpp src/core/rules/load.cpp src/core/rules/eval.cpp
            src/core/rules/match.cpp src/core/rules/attr_families_gen.cpp src/core/dict/codes.cpp
            src/core/dict/pos_tags_gen.cpp src/core/dict/pos_schema_gen.cpp src/core/dict/part_patterns_gen.cpp
            src/core/output/inflect.cpp src/core/output/ko_families_gen.cpp src/core/text/utf8.cpp src/core/text/unicode_gen.cpp)
        target_include_directories(hdrulec PRIVATE src)
        # The local rules live in the dictionary rows, so the checks read the program the
        # engine assembles from them; the CLI writes it once for the tests below.
        set(HDTRANS_RULE_DUMP "${CMAKE_CURRENT_BINARY_DIR}/rules-program.txt")
        add_test(NAME rules.dump COMMAND hdtrans_cli -dicts "${CMAKE_CURRENT_SOURCE_DIR}/dicts"
            --dump-rules "${HDTRANS_RULE_DUMP}" -in "${CMAKE_CURRENT_SOURCE_DIR}/tests/empty.in")
        set_tests_properties(rules.dump PROPERTIES FIXTURES_SETUP ruledump
            ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/rules-cache")
        # The warnings per flag are the counts tools/lint-budget.tsv records.
        add_test(NAME rules.lint COMMAND hdrulec lint "${HDTRANS_RULE_DUMP}" -dicts "${CMAKE_CURRENT_SOURCE_DIR}/dicts"
            -budget "${CMAKE_CURRENT_SOURCE_DIR}/tools/lint-budget.tsv")
        add_test(NAME rules.unknown-codes COMMAND "${CMAKE_COMMAND}"
            -DRULEC=$<TARGET_FILE:hdrulec> -DDICTS=${CMAKE_CURRENT_SOURCE_DIR}/dicts
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/rules-unknown-codes
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules_unknown_codes.cmake")
        add_test(NAME rules.lint-checks COMMAND "${CMAKE_COMMAND}" -DRULEC=$<TARGET_FILE:hdrulec>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/rules-lint-checks
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules_lint_checks.cmake")
        add_test(NAME rules.verify COMMAND hdrulec verify "${HDTRANS_RULE_DUMP}" -paths 4096)
        add_test(NAME rules.identity COMMAND hdrulec dump "${HDTRANS_RULE_DUMP}" -check "${HDTRANS_RULE_DIR}/program.hash")
        add_test(NAME rules.generated COMMAND hdrulec cpp "${HDTRANS_RULE_DUMP}" -globals
            -check "${CMAKE_CURRENT_SOURCE_DIR}/src/core/rules/compiled_gen.cpp")
        set_tests_properties(rules.lint rules.verify rules.identity rules.generated PROPERTIES
            FIXTURES_REQUIRED ruledump)
        # The rule tests are rows of tests/rules/cases/, run by rules.cases.themes.
    endif()

    # Translates mutated text (tests/fuzz/translate.cpp); tools/fuzz.py runs it. Other builds only
    # compile the source.
    if(HDTRANS_FUZZ)
        add_executable(hdtrans_fuzz tests/fuzz/translate.cpp)
        target_link_options(hdtrans_fuzz PRIVATE -fsanitize=fuzzer)
    else()
        add_library(hdtrans_fuzz OBJECT tests/fuzz/translate.cpp)
    endif()
    target_include_directories(hdtrans_fuzz PRIVATE src)
    target_link_libraries(hdtrans_fuzz PRIVATE hdtrans)

    # Dumps compiled dictionary entries and lookups over input lines.
    add_executable(snapshot_check tests/snapshot_check/main.cpp)
    target_include_directories(snapshot_check PRIVATE src)
    target_link_libraries(snapshot_check PRIVATE hdtrans)
endif()

if(EMSCRIPTEN)
    add_executable(hdtrans_wasm src/wasm/main.cpp src/frontend/dictionary_version.cpp)
    target_include_directories(hdtrans_wasm PRIVATE src)
    target_compile_definitions(hdtrans_wasm PRIVATE HDTRANS_VERSION="${HDTRANS_VERSION}")
    # Embind needs type information only in the binding translation unit.
    set_source_files_properties(src/wasm/main.cpp PROPERTIES COMPILE_OPTIONS -frtti)
    set_target_properties(hdtrans_wasm PROPERTIES OUTPUT_NAME hdtrans_engine)
    target_link_libraries(hdtrans_wasm PRIVATE hdtrans)
    target_link_options(hdtrans_wasm PRIVATE
        --bind --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1
        -sSTACK_SIZE=1048576
        -sEXPORTED_RUNTIME_METHODS=FS,HEAPU8)
elseif(WIN32)
    add_library(hdtrans_dll SHARED src/cshared/hdtrans_c.cpp src/frontend/module_dir.cpp src/cshared/hdtrans.def)
    set_target_properties(hdtrans_dll PROPERTIES OUTPUT_NAME hdTransEngine PREFIX "")
    target_compile_definitions(hdtrans_dll PRIVATE HDTRANS_BUILD_DLL)
    target_include_directories(hdtrans_dll PRIVATE src)
    target_link_libraries(hdtrans_dll PRIVATE hdtrans)

    add_library(j2kengine SHARED src/j2kengine/j2kengine.cpp src/frontend/module_dir.cpp src/j2kengine/j2kengine.def)
    set_target_properties(j2kengine PROPERTIES OUTPUT_NAME J2KEngine PREFIX "")
    target_include_directories(j2kengine PRIVATE src)
    target_link_libraries(j2kengine PRIVATE hdtrans)
    add_dependencies(config_host j2kengine)

    enable_language(RC)
    add_executable(hdtrans_gui WIN32 src/gui/main.cpp src/gui/hdtrans_gui.rc src/gui/hdtrans_gui.manifest
        src/frontend/module_dir.cpp)
    set_target_properties(hdtrans_gui PROPERTIES OUTPUT_NAME hdTransGUI)
    target_include_directories(hdtrans_gui PRIVATE src src/gui)
    target_compile_definitions(hdtrans_gui PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_libraries(hdtrans_gui PRIVATE hdtrans_gui_support comctl32 comdlg32 shell32 ole32 uuid gdi32 user32 imm32)
    hdtrans_version_resource(hdtrans_cli hdtrans.exe VFT_APP)
    hdtrans_version_resource(hdtrans_gui hdTransGUI.exe VFT_APP)
    set_source_files_properties(src/gui/main.cpp PROPERTIES COMPILE_DEFINITIONS HDTRANS_VERSION="${HDTRANS_VERSION}")
    hdtrans_version_resource(hdtrans_dll hdTransEngine.dll VFT_DLL)
    hdtrans_version_resource(j2kengine J2KEngine.dll VFT_DLL)
    if(MINGW)
        target_compile_definitions(hdtrans_gui PRIVATE HDTRANS_RC_MANIFEST)
        target_link_options(hdtrans_gui PRIVATE -municode)
    endif()
    add_test(NAME host.gui COMMAND hdtrans_gui --selftest)
    set_tests_properties(host.gui PROPERTIES TIMEOUT 180
        ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/gui-cache")

    add_executable(j2k_host tests/host/j2k_host.c)
    target_compile_definitions(j2k_host PRIVATE UNICODE _UNICODE)
    add_dependencies(j2k_host j2kengine hdtrans_dll)
    add_test(NAME host.j2k COMMAND j2k_host $<TARGET_FILE:j2kengine>)
    set_tests_properties(host.j2k PROPERTIES ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/host-cache")

    add_custom_command(TARGET j2kengine POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_FILE:j2kengine>
            $<TARGET_FILE_DIR:j2kengine>/J2KEngineH.dll VERBATIM)
    add_executable(tool_compat tests/host/tool_compat.cpp)
    add_dependencies(tool_compat j2kengine)
    set(toolCompatDir "${CMAKE_CURRENT_BINARY_DIR}/tool-compat-empty")
    file(MAKE_DIRECTORY "${toolCompatDir}")
    foreach(tool luna luna-ansi anemone anemone-ansi aral02 aral03 eztransserver)
        add_test(NAME host.${tool} COMMAND tool_compat ${tool} $<TARGET_FILE:j2kengine>)
        set_tests_properties(host.${tool} PROPERTIES WORKING_DIRECTORY "${toolCompatDir}"
            LABELS tool-compat TIMEOUT 120)
    endforeach()
    add_test(NAME host.eztransweb COMMAND tool_compat eztransweb $<TARGET_FILE_DIR:j2kengine>/J2KEngineH.dll)
    set_tests_properties(host.eztransweb PROPERTIES WORKING_DIRECTORY "${toolCompatDir}"
        LABELS tool-compat TIMEOUT 120)

    find_program(HDTRANS_CSC NAMES csc.exe
        PATHS "$ENV{WINDIR}/Microsoft.NET/Framework/v4.0.30319" NO_DEFAULT_PATH REQUIRED)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(toolCompatPlatform x86)
        foreach(tool vnr vnr-ansi)
            add_test(NAME host.${tool} COMMAND tool_compat ${tool} $<TARGET_FILE:j2kengine>)
            set_tests_properties(host.${tool} PROPERTIES WORKING_DIRECTORY "${toolCompatDir}"
                LABELS tool-compat TIMEOUT 120)
        endforeach()
    else()
        set(toolCompatPlatform x64)
    endif()
    set(xunityHost "${CMAKE_CURRENT_BINARY_DIR}/xunity_host.exe")
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests/host/xunity_host.cs" xunitySource)
    file(TO_NATIVE_PATH "${xunityHost}" xunityOutput)
    add_custom_command(OUTPUT "${xunityHost}"
        COMMAND "${HDTRANS_CSC}" /nologo /optimize+ /warnaserror+ /codepage:65001 /platform:${toolCompatPlatform}
            /out:${xunityOutput} "${xunitySource}"
        DEPENDS tests/host/xunity_host.cs VERBATIM)
    add_custom_target(xunity_host ALL DEPENDS "${xunityHost}")
    add_dependencies(xunity_host j2kengine)
    file(GENERATE OUTPUT "${xunityHost}.config" CONTENT
        "<?xml version=\"1.0\"?><configuration><runtime><NetFx40_PInvokeStackResilience enabled=\"1\"/></runtime></configuration>\n")
    add_test(NAME host.xunity COMMAND "${xunityHost}" $<TARGET_FILE:j2kengine>)
    set_tests_properties(host.xunity PROPERTIES WORKING_DIRECTORY "${toolCompatDir}"
        LABELS tool-compat TIMEOUT 120)
else()
    add_library(hdtrans_shared SHARED src/cshared/hdtrans_c.cpp src/frontend/module_dir.cpp)
    set_target_properties(hdtrans_shared PROPERTIES OUTPUT_NAME hdTransEngine)
    target_include_directories(hdtrans_shared PRIVATE src)
    target_link_libraries(hdtrans_shared PRIVATE hdtrans ${CMAKE_DL_LIBS})
endif()

# The 0.8 proof of concept (poc08/): the hdtrans08 CLI next to hdtrans. hdtrans08 is the 0.7.x
# engine built a second time with its hook points (src/core/engine/hooks.h) compiled in; the
# hdtrans library above has none of them.
option(HDTRANS_POC08 "Build the 0.8 proof of concept (hdtrans08)" ON)
if(HDTRANS_POC08 AND NOT EMSCRIPTEN)
    add_library(hdtrans_hooked STATIC ${HDTRANS_SOURCES}
        "${CMAKE_CURRENT_BINARY_DIR}/builtin_gen.cpp" "${CMAKE_CURRENT_BINARY_DIR}/builtin_extra_gen.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp" "${CMAKE_CURRENT_BINARY_DIR}/classes_gen.h")
    target_compile_definitions(hdtrans_hooked PUBLIC HDTRANS_POC08_HOOKS)
    if(HDTRANS_INTERPRET_RULES)
        target_compile_definitions(hdtrans_hooked PRIVATE HDTRANS_INTERPRET_RULES)
    endif()
    if(HDTRANS_PASS_EXPERIMENTS)
        target_compile_definitions(hdtrans_hooked PRIVATE HDTRANS_PASS_EXPERIMENTS)
    endif()
    # One target runs the generators, so that no build tool runs a generator twice at once for the two
    # libraries that compile its output.
    add_custom_target(hdtrans_generated DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/builtin_gen.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/builtin_extra_gen.cpp" "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/classes_gen.h")
    add_dependencies(hdtrans hdtrans_generated)
    add_dependencies(hdtrans_hooked hdtrans_generated)
    target_link_libraries(hdtrans_hooked PRIVATE pcre2-16 yaml PUBLIC Threads::Threads)
    target_include_directories(hdtrans_hooked PUBLIC include src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    set_target_properties(hdtrans_hooked PROPERTIES OUTPUT_NAME hdTransCoreHooked COMPILE_PDB_NAME hdTransCoreHooked)
    set(HDTRANS_CLI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/dictionary_version.cpp")
    add_subdirectory(poc08)
endif()
