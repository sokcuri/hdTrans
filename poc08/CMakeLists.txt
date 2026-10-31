# The 0.8 proof of concept: the IR, one library per component and the hdtrans08 CLI.
# A component directory with a CMakeLists.txt defines the library hd08_<component> with the
# functions of its stages (ir/stages.h); otherwise the driver links its stub.
add_library(hd08_ir STATIC ir/ir.cpp ir/json.cpp ir/ir_json.cpp ir/invariants.cpp)
target_include_directories(hd08_ir PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set(HD08_STAGE_LIBS)
set(HD08_STUB_SOURCES)
set(HD08_STUB_DEFS)
# Libraries without a stage of their own (the rule language, lexicon tables, learned weights);
# the stage components link the ones they use.
foreach(library rules lexicon learned)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${library}/CMakeLists.txt")
        add_subdirectory(${library})
    endif()
endforeach()
foreach(component analyzer structure discourse transfer generator)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${component}/CMakeLists.txt")
        add_subdirectory(${component})
        list(APPEND HD08_STAGE_LIBS hd08_${component})
    else()
        list(APPEND HD08_STUB_SOURCES driver/stubs/${component}.cpp)
        string(TOUPPER "${component}" upper)
        list(APPEND HD08_STUB_DEFS HD08_STUB_${upper})
    endif()
endforeach()

add_library(hd08_driver STATIC driver/driver.cpp driver/report.cpp ${HD08_STUB_SOURCES})
target_compile_definitions(hd08_driver PUBLIC ${HD08_STUB_DEFS})
target_link_libraries(hd08_driver PUBLIC ${HD08_STAGE_LIBS} hd08_ir)

# The side-IR driver: the stages of IR.md alone, on stubs where a component is missing.
add_executable(hdtrans08_ir driver/cli.cpp)
target_compile_definitions(hdtrans08_ir PRIVATE "HD08_DEFAULT_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
find_package(Threads REQUIRED)
target_link_libraries(hdtrans08_ir PRIVATE hd08_driver Threads::Threads)
set_target_properties(hdtrans08_ir PROPERTIES OUTPUT_NAME hdtrans08-ir)

# hdtrans08: the 0.7.x CLI on the hooked engine, with the 0.8 components at the hook points.
add_library(hd08_bridge STATIC bridge/bridge.cpp bridge/lift.cpp bridge/rule.cpp)
target_link_libraries(hd08_bridge PUBLIC hd08_driver hdtrans_hooked hd8gen)
add_executable(hdtrans08 ${HDTRANS_CLI_SOURCES})
target_compile_definitions(hdtrans08 PRIVATE "HDTRANS_VERSION_TEXT=\"${HDTRANS_VERSION_TEXT} poc08\""
    "HD08_DEFAULT_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
target_include_directories(hdtrans08 PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(hdtrans08 PRIVATE hd08_bridge)

file(GLOB HD08_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(hd08_tests ${HD08_TEST_SOURCES})
target_include_directories(hd08_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/unit")
target_compile_definitions(hd08_tests PRIVATE "HD08_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
target_link_libraries(hd08_tests PRIVATE hd08_driver)
add_test(NAME poc08.unit COMMAND hd08_tests)
find_program(HD08_PYTHON python3)
if(HD08_PYTHON AND TARGET hd8gen_cli)
    # the N5 grid file is what grid.py and its tables generate (the engine-pass test is separate)
    add_test(NAME poc08.grid.n5 COMMAND "${HD08_PYTHON}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/grid/grid.py"
        --hd8gen $<TARGET_FILE:hd8gen_cli> --check "${CMAKE_CURRENT_SOURCE_DIR}/tests/grid/n5.tsv")
endif()
if(HDTRANS_SH)
    add_test(NAME poc08.cli COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.sh" $<TARGET_FILE:hdtrans08_ir>)
    add_test(NAME poc08.hooks COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/hooks.sh" $<TARGET_FILE:hdtrans_cli>
        $<TARGET_FILE:hdtrans08> "${CMAKE_SOURCE_DIR}/dicts")
    if(TARGET hd8retro)
        add_test(NAME poc08.rule COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rule.sh" $<TARGET_FILE:hdtrans08>
            $<TARGET_FILE:hd8retro> "${CMAKE_SOURCE_DIR}/dicts")
    endif()
endif()
