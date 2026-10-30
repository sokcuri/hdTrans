# Lexicon component: the loader library and its test. The sources are the TSV files under lex/; the
# binary is compiled from them by tools/lexbin.py (Python, build time only).
cmake_minimum_required(VERSION 3.20)
if(NOT CMAKE_PROJECT_NAME)
    project(hd8_lexicon CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        add_compile_options(/EHs-c- /GR- /utf-8 /permissive- /W4 /WX)
    else()
        add_compile_options(-Wall -Wextra -Werror -fno-exceptions -fno-rtti)
    endif()
    enable_testing()
endif()

add_library(hd8_lexicon STATIC lexicon.cpp)
target_include_directories(hd8_lexicon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(hd8_lexicon PUBLIC cxx_std_20)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    set(fixture ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture)
    file(GLOB_RECURSE fixture_tsv CONFIGURE_DEPENDS ${fixture}/*.tsv)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fixture.hdlx ${CMAKE_CURRENT_BINARY_DIR}/fixture-names.hdlx
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/lexbin.py --lex ${fixture}
                --out ${CMAKE_CURRENT_BINARY_DIR}/fixture.hdlx --names-out ${CMAKE_CURRENT_BINARY_DIR}/fixture-names.hdlx
                --no-analysis -j 1
        DEPENDS ${fixture_tsv} ${CMAKE_CURRENT_SOURCE_DIR}/tools/lexbin.py
        VERBATIM)
    add_custom_target(hd8_lexicon_fixture DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fixture.hdlx)
    add_executable(hd8_lexicon_test lexicon_test.cpp)
    add_dependencies(hd8_lexicon_test hd8_lexicon_fixture)
    target_include_directories(hd8_lexicon_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/unit)
    target_compile_definitions(hd8_lexicon_test PRIVATE
        HD8_LEX_FIXTURE="${CMAKE_CURRENT_BINARY_DIR}/fixture.hdlx"
        HD8_LEX_FIXTURE_NAMES="${CMAKE_CURRENT_BINARY_DIR}/fixture-names.hdlx")
    target_link_libraries(hd8_lexicon_test PRIVATE hd8_lexicon)
    add_test(NAME poc08.lexicon COMMAND hd8_lexicon_test)
endif()
