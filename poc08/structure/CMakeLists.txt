# C (bunsetsu, heads) and L (clauses) of the 0.8 PoC. Inside poc08/ it defines hd08_structure with the
# stage functions of ir/stages.h; on its own (cmake -S poc08/structure) it builds the library, the dev tool
# hd8struct and the tests without the IR.
cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(hd8_structure LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    if(MSVC)
        add_compile_options(/W4 /WX /utf-8 /permissive- /EHs-c- /GR-)
        add_compile_definitions(_HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
    else()
        add_compile_options(-Wall -Wextra -Werror -fno-exceptions -fno-rtti -Wshadow)
    endif()
    enable_testing()
endif()

add_library(hd8_structure STATIC aux_table.cpp chunk.cpp heads.cpp clause.cpp)
target_include_directories(hd8_structure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(TARGET hd08_ir)
    add_library(hd08_structure STATIC stages.cpp)
    target_link_libraries(hd08_structure PUBLIC hd8_structure hd08_ir)
endif()

add_executable(hd8struct tools/st_tool.cpp)
target_link_libraries(hd8struct PRIVATE hd8_structure)

add_executable(hd8_structure_tests tests/structure_test.cpp)
target_link_libraries(hd8_structure_tests PRIVATE hd8_structure)
target_compile_definitions(hd8_structure_tests PRIVATE HD8_STRUCTURE_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
                           HD8_STRUCTURE_TESTS="${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_test(NAME poc08.structure COMMAND hd8_structure_tests)
