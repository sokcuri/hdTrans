# poc08/transfer: T, the construction matcher over the IR (DESIGN.md §3.3, §4.3).
add_library(hd08_transfer STATIC src/ir_view.cpp src/table.cpp src/match.cpp src/stage.cpp)
target_include_directories(hd08_transfer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hd08_transfer PUBLIC hd08_ir)

add_executable(hd8xfer tools/hd8xfer.cpp)
target_link_libraries(hd8xfer PRIVATE hd08_transfer)

add_test(NAME poc08.transfer.lint COMMAND hd8xfer --data "${CMAKE_CURRENT_SOURCE_DIR}/data" --lint)
