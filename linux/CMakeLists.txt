set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Network Concurrent)
find_package(KF6GlobalAccel REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(LayerShellQt REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Network Test)
add_library(phonomenal-native STATIC ../tts_phonomenal.cpp ../extern/phonomenal/cpp/src/splicer.cpp)
target_include_directories(phonomenal-native PUBLIC .. ../extern/phonomenal/cpp/include)
target_link_libraries(phonomenal-native PRIVATE ${CMAKE_DL_LIBS})
add_executable(vctts main.cpp)
target_link_libraries(vctts PRIVATE phonomenal-native Qt6::Widgets Qt6::Multimedia Qt6::Network Qt6::Concurrent Qt6::Test KF6::GlobalAccel KF6::WindowSystem LayerShellQt::Interface)
add_executable(test-phonomenal-linux ../tests/test_phonomenal.cpp)
target_compile_options(test-phonomenal-linux PRIVATE -UNDEBUG)
target_link_libraries(test-phonomenal-linux PRIVATE phonomenal-native)
enable_testing()
add_test(NAME phonomenal-native COMMAND test-phonomenal-linux)
install(TARGETS vctts DESTINATION bin)

# Cross-build the small Windows COM worker when MinGW is available.
find_program(VCTTS_MINGW_CXX x86_64-w64-mingw32-g++)
if(VCTTS_MINGW_CXX)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/speech11-bridge.exe
    COMMAND ${VCTTS_MINGW_CXX} -std=c++17 -O2 -static -municode
      ${CMAKE_CURRENT_SOURCE_DIR}/speech11_bridge.cpp -lole32 -loleaut32 -lsapi -luuid
      -o ${CMAKE_CURRENT_BINARY_DIR}/speech11-bridge.exe
    DEPENDS speech11_bridge.cpp VERBATIM)
  add_custom_target(vctts-sapi-bridge ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/speech11-bridge.exe)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/speech11-bridge.exe DESTINATION share/vctts/sapi)
endif()
install(PROGRAMS vctts-audio DESTINATION bin)
install(PROGRAMS setup_microsoft_voices.py DESTINATION share/vctts/sapi)
