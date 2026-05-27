cmake_minimum_required(VERSION 3.20)

# On macOS, default to the host CPU arch. Without this an x86_64-built cmake
# binary (e.g. from /usr/local) will silently produce x86_64 objects on Apple
# Silicon, which then fail to link against arm64 Homebrew libs. We use sysctl
# rather than `uname -m`/CMAKE_HOST_SYSTEM_PROCESSOR because those reflect the
# running process arch, not the hardware.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    execute_process(COMMAND sysctl -n hw.optional.arm64
        OUTPUT_VARIABLE _is_arm64
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_is_arm64 STREQUAL "1")
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS")
    else()
        set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for macOS")
    endif()
endif()

project(miditoqwerty)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(SDL2 REQUIRED)
find_package(PortMidi REQUIRED)

# craigsapp/midifile: SMF parser used by the built-in file player. Pinned by
# submodule. Build just the library, skip the 50+ CLI tools. Override the
# policy version so the submodule's `cmake_minimum_required(2.8)` doesn't
# get rejected by modern CMake.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_MIDILIBRARY_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory(midifile)

# Generate gl3w headers and sources (runs the submodule's own CMake). The
# submodule pins to CMake 2.x compatibility which modern CMake refuses by
# default, and uses the removed FindPythonInterp module — re-allow the
# legacy policy version and feed PYTHON_EXECUTABLE through ourselves.
find_program(PYTHON3_EXECUTABLE NAMES python3 python REQUIRED)
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DPYTHON_EXECUTABLE=${PYTHON3_EXECUTABLE}
        .
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/gl3w)
execute_process(
    COMMAND ${CMAKE_COMMAND} --build .
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/gl3w)

set(gl3w_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gl3w/include)
add_library(gl3w ${CMAKE_CURRENT_SOURCE_DIR}/gl3w/src/gl3w.c)
target_include_directories(gl3w PUBLIC ${gl3w_INCLUDE_DIR})

add_definitions(-DIMGUI_IMPL_OPENGL_LOADER_GL3W)
if(NOT MSVC)
    add_compile_options(-Wall)
endif()

include_directories(
    ${gl3w_INCLUDE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${PORTMIDI_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/imgui
    ${CMAKE_SOURCE_DIR}/imgui/backends
    ${CMAKE_SOURCE_DIR}/midifile/include
    ${CMAKE_SOURCE_DIR}/src)

add_library(imgui
    imgui/backends/imgui_impl_sdl.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp)

# --- Source lists -----------------------------------------------------------

set(MTQ_CORE_SOURCES
    src/main.cpp
    src/Log.cpp
    src/Midi.cpp
    src/Piano.cpp
    src/settings.cpp
    src/themes.cpp
    src/util.cpp
    src/core/NoteRouter.cpp
    src/midi/MidiFilePlayer.cpp
    src/platform/InputBackend.cpp
    imgui/backends/imgui_impl_sdl.cpp)

set(MTQ_PLATFORM_SOURCES "")

if(WIN32)
    list(APPEND MTQ_PLATFORM_SOURCES
        src/inpututils.cpp
        src/platform/win/WindowsInputBackend.cpp
        src/platform/win/WindowsOverlay.cpp
        src/platform/win/WindowsHotkey.cpp
        src/platform/win/WindowsFileDialog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/res/resource.rc)
elseif(APPLE)
    list(APPEND MTQ_PLATFORM_SOURCES
        src/platform/mac/MacInputBackend.mm
        src/platform/mac/MacOverlay.mm
        src/platform/mac/MacHotkey.mm
        src/platform/mac/MacFileDialog.mm)
endif()

# --- Executable -------------------------------------------------------------

if(WIN32)
    add_executable(miditoqwerty WIN32
        ${MTQ_CORE_SOURCES}
        ${MTQ_PLATFORM_SOURCES})
elseif(APPLE)
    add_executable(miditoqwerty
        ${MTQ_CORE_SOURCES}
        ${MTQ_PLATFORM_SOURCES})
    set_source_files_properties(src/platform/mac/MacInputBackend.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_source_files_properties(src/platform/mac/MacOverlay.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_source_files_properties(src/platform/mac/MacHotkey.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_source_files_properties(src/platform/mac/MacFileDialog.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    add_executable(miditoqwerty
        ${MTQ_CORE_SOURCES}
        ${MTQ_PLATFORM_SOURCES})
endif()

target_include_directories(miditoqwerty PRIVATE src imgui imgui/backends)

# --- Linking ----------------------------------------------------------------

if(WIN32)
    # Original behaviour: link SDL2.lib by path. Keep for parity until we
    # confirm the modern SDL2::SDL2 target works in the user's MSVC project.
    target_link_libraries(miditoqwerty
        ${PORTMIDI_LIBRARY}
        ${SDL2_DIR}/lib/x64/SDL2.lib
        ${SDL2_DIR}/lib/x64/SDL2main.lib
        imgui
        gl3w
        midifile
        comdlg32                  # GetOpenFileNameA
        ${CMAKE_DL_LIBS})
elseif(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    find_library(OPENGL_FRAMEWORK OpenGL)
    find_library(APPSERVICES_FRAMEWORK ApplicationServices)
    find_library(CARBON_FRAMEWORK Carbon)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)

    # Homebrew's modern SDL2 exports SDL2::SDL2; fall back to legacy variables.
    if(TARGET SDL2::SDL2)
        set(MTQ_SDL2_LINK SDL2::SDL2)
    else()
        set(MTQ_SDL2_LINK ${SDL2_LIBRARIES})
    endif()

    target_link_libraries(miditoqwerty
        ${PORTMIDI_LIBRARIES}
        ${MTQ_SDL2_LINK}
        imgui
        gl3w
        midifile
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${OPENGL_FRAMEWORK}
        ${APPSERVICES_FRAMEWORK}
        ${CARBON_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${CMAKE_DL_LIBS})
    target_link_libraries(imgui PUBLIC ${MTQ_SDL2_LINK})
else()
    target_link_libraries(miditoqwerty
        ${PORTMIDI_LIBRARIES}
        ${SDL2_LIBRARIES}
        imgui
        gl3w
        midifile
        ${CMAKE_DL_LIBS})
endif()
