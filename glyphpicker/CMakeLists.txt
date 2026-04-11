cmake_minimum_required(VERSION 3.16)
project(GlyphPicker)

set(CMAKE_CXX_STANDARD 17)

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++ -municode")
endif()

# Extract imageres.dll icon #81 into build dir
set(ICON_OUT "${CMAKE_CURRENT_BINARY_DIR}/app.ico")
execute_process(
    COMMAND powershell -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/extract_icon.ps1"
            -OutPath "${ICON_OUT}"
            -DllName "imageres.dll"
            -Index   81
    RESULT_VARIABLE _icon_result
)
if(NOT _icon_result EQUAL 0)
    message(WARNING "Icon extraction failed; exe will have no embedded icon.")
endif()

# Copy manifest to build dir
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
               "${CMAKE_CURRENT_BINARY_DIR}/app.manifest" COPYONLY)

# Generate .rc with absolute paths
set(RC_GEN "${CMAKE_CURRENT_BINARY_DIR}/icon_gen.rc")
file(WRITE "${RC_GEN}"
"IDI_APPICON ICON \"${CMAKE_CURRENT_BINARY_DIR}/app.ico\"\n"
"1 24 \"${CMAKE_CURRENT_BINARY_DIR}/app.manifest\"\n"
)

set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I\"${CMAKE_CURRENT_BINARY_DIR}\"")

if(EXISTS "${ICON_OUT}")
    set(RESOURCE_FILES "${RC_GEN}")
else()
    set(RESOURCE_FILES "")
endif()

add_executable(glyphpicker WIN32 glyphpicker.cpp ${RESOURCE_FILES})
target_link_libraries(glyphpicker comctl32 shell32 imm32 d2d1 dwrite)
