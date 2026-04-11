cmake_minimum_required(VERSION 3.16)
project(Stopwatch)

set(CMAKE_CXX_STANDARD 17)

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -municode")
endif()

# Extract shell32 icon #249 → app.ico in the BUILD directory
# Re-extract every time so sizes are always current
set(ICON_OUT "${CMAKE_CURRENT_BINARY_DIR}/app.ico")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/extract_icon.ps1"
            -OutPath "${ICON_OUT}"
    RESULT_VARIABLE _icon_result
)
if(NOT _icon_result EQUAL 0)
    message(WARNING "Icon extraction failed (code ${_icon_result}); exe will have no embedded icon.")
endif()

# Copy manifest to build dir so windres can find it
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
               "${CMAKE_CURRENT_BINARY_DIR}/app.manifest" COPYONLY)

# Generate .rc in the build directory with absolute paths
set(RC_IN  "${CMAKE_CURRENT_SOURCE_DIR}/icon.rc")
set(RC_GEN "${CMAKE_CURRENT_BINARY_DIR}/icon_gen.rc")
file(WRITE "${RC_GEN}"
"IDI_APPICON ICON \"${CMAKE_CURRENT_BINARY_DIR}/app.ico\"\n"
"1 24 \"${CMAKE_CURRENT_BINARY_DIR}/app.manifest\"\n"
)

# Tell windres where to look for includes (the binary dir)
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I\"${CMAKE_CURRENT_BINARY_DIR}\"")

if(EXISTS "${ICON_OUT}")
    set(RESOURCE_FILES "${RC_GEN}")
else()
    set(RESOURCE_FILES "")
    message(WARNING "No app.ico found — skipping resource.")
endif()

add_executable(stopwatch WIN32 stopwatch.cpp ${RESOURCE_FILES})

target_link_libraries(stopwatch comctl32 comdlg32 shell32)
