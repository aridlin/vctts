cmake_minimum_required(VERSION 3.20)
project(tts_overlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------
# Static MSVC runtime (/MT)
# -------------------------
# This is THE important part for portability
if (MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# -------------------------
# Paths
# -------------------------
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/extern/imgui")

# cppwinrt headers from Windows SDK (header-only)
set(CPPWINRT_INCLUDE
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/cppwinrt"
)

# -------------------------
# Executable
# -------------------------
add_executable(tts_overlay WIN32
    main.cpp
    win32_window.cpp
    d3d11_renderer.cpp
    imgui_ui.cpp
    keyboard_hook.cpp
    tts_winrt.cpp
    tts_keyless.cpp
    audio_devices.cpp
    audio_playback.cpp
    icon.rc

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)


# -------------------------
# Includes
# -------------------------
target_include_directories(tts_overlay PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/extern/miniaudio
    ${CPPWINRT_INCLUDE}
)

# -------------------------
# Compile definitions
# -------------------------
target_compile_definitions(tts_overlay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# -------------------------
# Link libraries
# -------------------------
target_link_libraries(tts_overlay PRIVATE
    d3d11
    dxgi
    d3dcompiler
    user32
    gdi32
    imm32
    ole32
    oleaut32
    shell32
    uuid
    winmm
    propsys
    avrt
    winhttp
)

# -------------------------
# Tests
# -------------------------
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_executable(test_app_state tests/test_app_state.cpp)
    target_include_directories(test_app_state PRIVATE
        ${CMAKE_SOURCE_DIR}
    )
    if(NOT WIN32)
        target_include_directories(test_app_state PRIVATE
            ${CMAKE_SOURCE_DIR}/tests/include
        )
    endif()
    if(UNIX)
        target_link_libraries(test_app_state PRIVATE pthread)
    endif()
    add_test(NAME AppStateTest COMMAND test_app_state)
endif()
