cmake_minimum_required(VERSION 3.15)
project(ScrollbarTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ScrollbarTest WIN32
    test_scrollbar.cpp
    ../my_scrollbar.cpp
)

target_include_directories(ScrollbarTest PRIVATE ..)

target_link_libraries(ScrollbarTest
    comctl32
    user32
    gdi32
)

target_compile_definitions(ScrollbarTest PRIVATE UNICODE _UNICODE)

if(MINGW)
    target_link_options(ScrollbarTest PRIVATE -municode -mwindows)
endif()
