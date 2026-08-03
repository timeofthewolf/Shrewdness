find_package(Threads REQUIRED)

add_library(net http.cpp)

target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(net PUBLIC cxx_std_20)
target_link_libraries(net PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(net PUBLIC ws2_32)
    # Winsock needs Vista or later for inet_ntop.
    target_compile_definitions(net PUBLIC _WIN32_WINNT=0x0600 WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
