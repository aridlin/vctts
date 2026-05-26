cmake_minimum_required(VERSION 3.20)
project(vctts LANGUAGES CXX)

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
# Executable
# -------------------------
add_executable(vctts WIN32
    main.cpp
    win32_window.cpp
    native_ui.cpp
    keyboard_hook.cpp
    app_audio_control.cpp
    incoming_translator.cpp
    tts_winrt.cpp
    tts_keyless.cpp
    translator.cpp
    custom_tts.cpp
    audio_devices.cpp
    audio_playback.cpp
    icon.rc
)


# -------------------------
# Includes
# -------------------------
target_include_directories(vctts PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/extern/miniaudio
)

# -------------------------
# Tests
# -------------------------
enable_testing()
add_executable(test_custom test_custom.cpp custom_tts.cpp)
target_include_directories(test_custom PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME test_custom COMMAND test_custom)

# -------------------------
# Compile definitions
# -------------------------
target_compile_definitions(vctts PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# -------------------------
# Link libraries
# -------------------------
target_link_libraries(vctts PRIVATE
    user32
    gdi32
    gdiplus
    dwmapi
    imm32
    ole32
    oleaut32
    shell32
    uuid
    winmm
    propsys
    avrt
    winhttp
    runtimeobject
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
