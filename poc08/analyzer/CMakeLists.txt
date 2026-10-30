# 0.8 analyzer: N (layout normalization) and A (lattice analysis).
# hd08_analyzer_core is the analyzer alone (no IR dependency, also for WASM); hd08_analyzer adds the stages.
add_library(hd08_analyzer_core STATIC
    src/analyzer.cpp src/lattice.cpp src/model.cpp src/model_tsv.cpp src/normalize.cpp)
target_include_directories(hd08_analyzer_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
                                              PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(hd08_analyzer_core PUBLIC cxx_std_20)

add_library(hd08_analyzer STATIC src/stage.cpp)
target_include_directories(hd08_analyzer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hd08_analyzer PUBLIC hd08_analyzer_core hd08_ir)

add_executable(hd8ana tools/hd8ana.cpp)
target_include_directories(hd8ana PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hd8ana PRIVATE hd08_analyzer_core)

add_executable(hd08_analyzer_tests tests/analyzer_test.cpp)
target_include_directories(hd08_analyzer_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/tests/unit")
target_compile_definitions(hd08_analyzer_tests PRIVATE "HD08_ANALYZER_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")
target_link_libraries(hd08_analyzer_tests PRIVATE hd08_analyzer_core)
add_test(NAME poc08.analyzer COMMAND hd08_analyzer_tests)
