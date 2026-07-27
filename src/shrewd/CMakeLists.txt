add_library(shrewd
    isa.cpp
    io.cpp
    genome.cpp
    vm.cpp
    asm.cpp
)

target_include_directories(shrewd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(shrewd PUBLIC cxx_std_20)
