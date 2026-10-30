# Standalone build of the decoder CLI without the 0.7.x engine (no --rule-dicts), for the WASM checks:
#   emcmake cmake -S poc08/smt/wasm -B build/smt-wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build/smt-wasm
#   node build/smt-wasm/hd8smt.js decode --model DIR --analyzer CORE.bin < ja > ko
cmake_minimum_required(VERSION 3.25)
project(hd8smt_wasm CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror -fno-exceptions -fno-rtti -ffp-contract=off)
set(P "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(GLOB GEN_SRC "${P}/generator/src/*.cpp")
list(FILTER GEN_SRC EXCLUDE REGEX "stages_hd08")
add_library(gen STATIC ${GEN_SRC})
target_include_directories(gen PUBLIC "${P}/generator/include" PRIVATE "${P}/generator/src")
add_library(ana STATIC ${P}/analyzer/src/analyzer.cpp ${P}/analyzer/src/lattice.cpp ${P}/analyzer/src/model.cpp
    ${P}/analyzer/src/model_tsv.cpp ${P}/analyzer/src/normalize.cpp)
target_include_directories(ana PUBLIC "${P}/analyzer/include" PRIVATE "${P}/analyzer/src")
add_library(smt STATIC ${P}/smt/src/ko.cpp ${P}/smt/src/lm.cpp ${P}/smt/src/ptable.cpp ${P}/smt/src/decoder.cpp)
target_include_directories(smt PUBLIC "${P}/smt/include" PRIVATE "${P}/smt/src")
target_link_libraries(smt PUBLIC gen ana)
add_executable(hd8smt ${P}/smt/tools/hd8smt.cpp)
target_compile_definitions(hd8smt PRIVATE HD8SMT_NO_RULE "HD8SMT_GEN_DATA=\"${P}/generator/data/ko\""
    "HD8SMT_REG_DATA=\"${P}/generator/data/register\"")
target_link_libraries(hd8smt PRIVATE smt)
if(EMSCRIPTEN)
    target_link_options(hd8smt PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sSTACK_SIZE=4MB)
endif()
