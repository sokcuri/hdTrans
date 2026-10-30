# S, Z and R of the 0.8 PoC (W5). The register model is in poc08/generator (library hd8gen); this library adapts it
# to the stage API.
add_library(hd08_discourse STATIC src/stages.cpp)
target_link_libraries(hd08_discourse PUBLIC hd08_ir hd8gen)
