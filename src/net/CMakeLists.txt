find_package(Threads REQUIRED)

add_library(net http.cpp)

target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(net PUBLIC cxx_std_20)
target_link_libraries(net PUBLIC Threads::Threads)
