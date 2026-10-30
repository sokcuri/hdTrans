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

add_executable(hdtrans08 driver/cli.cpp)
target_compile_definitions(hdtrans08 PRIVATE "HD08_DEFAULT_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
target_link_libraries(hdtrans08 PRIVATE hd08_driver)
if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(hdtrans08 PRIVATE Threads::Threads)
endif()

file(GLOB HD08_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(hd08_tests ${HD08_TEST_SOURCES})
target_include_directories(hd08_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/unit")
target_compile_definitions(hd08_tests PRIVATE "HD08_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
target_link_libraries(hd08_tests PRIVATE hd08_driver)
add_test(NAME poc08.unit COMMAND hd08_tests)
add_test(NAME poc08.cli COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.sh" $<TARGET_FILE:hdtrans08>)
