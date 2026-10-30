# 0.8 phrase-based layer: Korean token notation and recombination (via the W5 generator), the monotone
# lattice decoder, the n-gram LM and the phrase table readers. Build tools live in tools/*.py.
add_library(hd08_smt STATIC src/ko.cpp src/lm.cpp src/ptable.cpp src/decoder.cpp)
target_include_directories(hd08_smt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(hd08_smt PUBLIC cxx_std_20)
target_link_libraries(hd08_smt PUBLIC hd8gen hd08_analyzer_core)
if(MSVC)
    target_compile_options(hd08_smt PRIVATE /W4 /WX)
else()
    target_compile_options(hd08_smt PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror -ffp-contract=off)
endif()

add_executable(hd8smt tools/hd8smt.cpp)
target_link_libraries(hd8smt PRIVATE hd08_smt hdtrans)
target_include_directories(hd8smt PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(hd8smt PRIVATE "HD8SMT_GEN_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/../generator/data/ko\""
    "HD8SMT_REG_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/../generator/data/register\"")
if(EMSCRIPTEN)
    target_link_options(hd8smt PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
endif()
