cmake_minimum_required(VERSION 3.15)
project(SkeletonApp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output to build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(RESOURCE_FILES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/SetupCraft.rc")
    list(APPEND RESOURCE_FILES "${CMAKE_SOURCE_DIR}/SetupCraft.rc")
endif()

add_executable(SkeletonApp WIN32
    main.cpp
    languages.cpp
    db.cpp
    button.cpp
    ${RESOURCE_FILES}
)

# Produce an executable named SetupCraft.exe (so packaging and icon names match)
set_target_properties(SkeletonApp PROPERTIES OUTPUT_NAME "SetupCraft")

target_link_libraries(SkeletonApp
    comctl32
    user32
    gdi32
    shell32
)

target_compile_definitions(SkeletonApp PRIVATE UNICODE _UNICODE)

if(MINGW)
    target_link_options(SkeletonApp PRIVATE -municode -mwindows)
endif()
