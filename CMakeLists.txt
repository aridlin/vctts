cmake_minimum_required(VERSION 3.20)
project(tts_overlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/extern/imgui")

add_executable(tts_overlay WIN32
    main.cpp
    win32_window.cpp
    d3d11_renderer.cpp
    audio_playback.cpp
    audio_devices.cpp
    imgui_ui.cpp
    keyboard_hook.cpp
    tts_sapi.cpp

    # Dear ImGui core
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp

    # Dear ImGui backends (Win32 + D3D11)
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(tts_overlay PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/extern/miniaudio
)

# Force wide-char WinAPI
target_compile_definitions(tts_overlay PRIVATE
    UNICODE
    _UNICODE
)

# D3D11 + WinAPI + COM/audio bits (you already use these)
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
    sapi
    avrt
)

