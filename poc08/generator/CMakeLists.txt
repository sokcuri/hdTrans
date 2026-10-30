# 0.8 PoC: Korean side (W rewrite, G generate, and the R register model). Builds standalone or as a
# subdirectory of the repository (add_subdirectory(poc08/generator)).
cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED PROJECT_NAME)
    project(hd8gen CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        set(CMAKE_CXX_FLAGS "/EHs-c- /GR- /utf-8 /permissive- /W4 /WX /Zc:inline")
    else()
        add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror -fno-exceptions -fno-rtti)
    endif()
endif()

add_library(hd8gen STATIC
    src/hangul.cpp
    src/tables.cpp
    src/conj.cpp
    src/realize.cpp
    src/json.cpp
    src/ir_json.cpp
    src/register.cpp
    src/kir_text.cpp
    src/rewrite.cpp
    src/analyze.cpp
)
target_include_directories(hd8gen PUBLIC include PRIVATE src)
target_compile_features(hd8gen PUBLIC cxx_std_20)

add_executable(hd8gen_cli tools/hd8gen.cpp)
set_target_properties(hd8gen_cli PROPERTIES OUTPUT_NAME hd8gen)
target_link_libraries(hd8gen_cli PRIVATE hd8gen)
target_compile_definitions(hd8gen_cli PRIVATE HD8GEN_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_executable(hd8retro tools/retro.cpp)
target_link_libraries(hd8retro PRIVATE hd8gen)
target_compile_definitions(hd8retro PRIVATE HD8GEN_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
endif()
add_test(NAME hd8gen_gold COMMAND hd8gen_cli --data ${CMAKE_CURRENT_SOURCE_DIR}/data gold ${CMAKE_CURRENT_SOURCE_DIR}/tests/gold/gold.tsv)
add_test(NAME hd8gen_gen COMMAND hd8gen_cli --data ${CMAKE_CURRENT_SOURCE_DIR}/data gen ${CMAKE_CURRENT_SOURCE_DIR}/tests/gen/cases.jsonl)
add_test(NAME hd8gen_rewrite_lint COMMAND hd8gen_cli --data ${CMAKE_CURRENT_SOURCE_DIR}/data lint)
