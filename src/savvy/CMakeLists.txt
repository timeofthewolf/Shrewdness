add_library(savvy
    parser.cpp
    compiler.cpp
    decompile.cpp
)

target_include_directories(savvy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(savvy PUBLIC shrewd)
