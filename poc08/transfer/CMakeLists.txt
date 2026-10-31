# poc08/transfer: the native transfer rows, the deciders at decision points over 0.7.x's decision (られる voice: a
# hand-written rule table; ため, ように, ところで: W4 learned weights), plus the research construction matcher
# (superseded, kept as the inventory's reference).
add_library(hd08_transfer STATIC src/ir_view.cpp src/table.cpp src/match.cpp src/decide.cpp src/native.cpp src/stage.cpp)
target_include_directories(hd08_transfer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hd08_transfer PUBLIC hd08_ir)
if(TARGET hd08_structure)
  target_link_libraries(hd08_transfer PUBLIC hd08_structure)
endif()

add_executable(hd8xfer tools/hd8xfer.cpp)
target_link_libraries(hd8xfer PRIVATE hd08_transfer)
add_executable(hd8decide tools/hd8decide.cpp)
target_link_libraries(hd8decide PRIVATE hd08_transfer)

if(HDTRANS_SH)
  add_test(NAME poc08.transfer.decide
           COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/decide_test.sh" $<TARGET_FILE:hd8decide>
                   "${CMAKE_CURRENT_SOURCE_DIR}/data" "${CMAKE_CURRENT_SOURCE_DIR}/tests")
endif()
