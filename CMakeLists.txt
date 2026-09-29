cmake_minimum_required(VERSION 3.28)
project(obs-quick-record VERSION 0.1.0 LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Windows x64 MSVC and the OBS Qt SDK are required")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(OBS_SOURCE_DIR "" CACHE PATH "OBS 32.2.2 source")
set(OBS_SDK_DIR "" CACHE PATH "Directory containing OBS import libraries")
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets)
find_library(OBS_LIBRARY NAMES obs HINTS "${OBS_SDK_DIR}" REQUIRED NO_DEFAULT_PATH)
find_library(OBS_FRONTEND_LIBRARY NAMES obs-frontend-api HINTS "${OBS_SDK_DIR}" REQUIRED NO_DEFAULT_PATH)
set(OBS_RELEASE_CANDIDATE 0)
set(OBS_BETA 0)
configure_file("${OBS_SOURCE_DIR}/libobs/obsconfig.h.in" "${CMAKE_CURRENT_BINARY_DIR}/obsconfig.h")
add_library(obs-quick-record MODULE
  src/plugin-main.cpp
  src/quick-record-controller.cpp
  src/hotkey-manager.cpp
  src/capture-controller.cpp
  src/settings.cpp
  src/settings-window.cpp
  src/quick-record-overlay.cpp
  src/monitor-selector.cpp
  src/region-selector.cpp
  src/window-selector.cpp
  src/recording-indicator.cpp
)
target_include_directories(obs-quick-record PRIVATE "${OBS_SOURCE_DIR}/libobs" "${OBS_SOURCE_DIR}/frontend/api" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(obs-quick-record PRIVATE "${OBS_LIBRARY}" "${OBS_FRONTEND_LIBRARY}" Qt6::Widgets dwmapi)
target_compile_definitions(obs-quick-record PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(obs-quick-record PRIVATE /W4 /utf-8 /permissive-)
set_target_properties(obs-quick-record PROPERTIES PREFIX "")
install(TARGETS obs-quick-record RUNTIME DESTINATION obs-quick-record/bin/64bit LIBRARY DESTINATION obs-quick-record/bin/64bit)
install(DIRECTORY data/ DESTINATION obs-quick-record/data)
install(FILES LICENSE README.md DESTINATION obs-quick-record)
install(DIRECTORY docs/ DESTINATION obs-quick-record/docs)
include(CTest)
if(BUILD_TESTING)
  add_executable(capture-target WIN32 tests/capture-target.cpp)
  target_link_libraries(capture-target PRIVATE Qt6::Widgets)
  add_executable(geometry-check tests/geometry-check.cpp src/monitor-selector.cpp src/region-selector.cpp src/window-selector.cpp)
  target_include_directories(geometry-check PRIVATE src)
  target_link_libraries(geometry-check PRIVATE Qt6::Widgets dwmapi)
  target_compile_options(geometry-check PRIVATE /utf-8 /UNDEBUG)
  add_test(NAME geometry-check COMMAND geometry-check)
  set_tests_properties(geometry-check PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=minimal")
  add_executable(hotkey-check tests/hotkey-check.cpp src/hotkey-manager.cpp src/settings-window.cpp)
  target_include_directories(hotkey-check PRIVATE src "${OBS_SOURCE_DIR}/libobs" "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(hotkey-check PRIVATE "${OBS_LIBRARY}" Qt6::Widgets)
  target_compile_options(hotkey-check PRIVATE /utf-8 /UNDEBUG)
  add_test(NAME hotkey-check COMMAND hotkey-check)
  set_tests_properties(hotkey-check PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=minimal" TIMEOUT 60)
endif()
