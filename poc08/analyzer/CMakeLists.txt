# 0.8 analyzer: N (layout normalization) and A (lattice analysis).
# hd08_analyzer_core is the analyzer alone (no IR dependency); hd08_analyzer adds the stages.
# The folder also configures on its own (cmake -S poc08/analyzer), which is how the WASM build is checked.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(hd08_analyzer CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        add_compile_options(/W4 /WX)
    else()
        add_compile_options(-Wall -Wextra -Wshadow -Werror -fno-exceptions -fno-rtti)
    endif()
    enable_testing()
    set(HD08_ANALYZER_STANDALONE ON)
    set(HD08_TEST_UNIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/unit")
else()
    set(HD08_TEST_UNIT_DIR "${CMAKE_SOURCE_DIR}/tests/unit")
endif()
add_library(hd08_analyzer_core STATIC
    src/analyzer.cpp src/lattice.cpp src/model.cpp src/model_tsv.cpp src/normalize.cpp)
target_include_directories(hd08_analyzer_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
                                              PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(hd08_analyzer_core PUBLIC cxx_std_20)

if(NOT HD08_ANALYZER_STANDALONE)
    add_library(hd08_analyzer STATIC src/stage.cpp)
    target_include_directories(hd08_analyzer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(hd08_analyzer PUBLIC hd08_analyzer_core hd08_ir)
endif()

add_executable(hd8ana tools/hd8ana.cpp)
target_include_directories(hd8ana PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hd8ana PRIVATE hd08_analyzer_core)

add_executable(hd08_analyzer_tests tests/analyzer_test.cpp)
target_include_directories(hd08_analyzer_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${HD08_TEST_UNIT_DIR}")
target_compile_definitions(hd08_analyzer_tests PRIVATE "HD08_ANALYZER_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")
target_link_libraries(hd08_analyzer_tests PRIVATE hd08_analyzer_core)
add_test(NAME poc08.analyzer COMMAND hd08_analyzer_tests)
add_test(NAME poc08.analyzer.cases COMMAND hd8ana cases "${CMAKE_CURRENT_SOURCE_DIR}/data" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cases.tsv")

if(EMSCRIPTEN)
    # node runs the tool and the tests against the host file system
    target_link_options(hd8ana PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
    target_link_options(hd08_analyzer_tests PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
endif()
