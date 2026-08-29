cmake_minimum_required(VERSION 3.21)

project(CamBridgeNativeMvp LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "CamBridge Native MVP requires Windows")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cambridge_frame_ipc STATIC
  src/frame_ipc.cpp
)
target_include_directories(cambridge_frame_ipc PUBLIC include)
target_compile_definitions(cambridge_frame_ipc PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cambridge_frame_ipc PUBLIC kernel32)

add_library(cambridge_media_source SHARED
  virtual-camera/diagnostic_log.cpp
  virtual-camera/cambridge_media_source.cpp
  virtual-camera/dllmain.cpp
)
set_target_properties(cambridge_media_source PROPERTIES LINK_FLAGS
  "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/virtual-camera/cambridge_media_source.def")
target_include_directories(cambridge_media_source PRIVATE include virtual-camera)
target_compile_definitions(cambridge_media_source PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cambridge_media_source PRIVATE cambridge_frame_ipc mfplat mfuuid mfsensorgroup shlwapi ole32 advapi32)

add_executable(cambridge_frame_ipc_tests
  tests/frame_ipc_tests.cpp
)
target_link_libraries(cambridge_frame_ipc_tests PRIVATE cambridge_frame_ipc)

add_executable(cambridge_synthetic_publisher
  src/synthetic_frame_publisher.cpp
)
target_link_libraries(cambridge_synthetic_publisher PRIVATE cambridge_frame_ipc)

add_executable(cambridge_virtual_camera_manager
  virtual-camera/virtual_camera_manager.cpp
)
target_include_directories(cambridge_virtual_camera_manager PRIVATE include virtual-camera)
target_compile_definitions(cambridge_virtual_camera_manager PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cambridge_virtual_camera_manager PRIVATE cambridge_frame_ipc mfsensorgroup mfplat mfuuid ole32 advapi32 shlwapi)

add_executable(cambridge_capture_probe
  tests/capture_probe.cpp
)
target_compile_definitions(cambridge_capture_probe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cambridge_capture_probe PRIVATE cambridge_frame_ipc mf mfplat mfreadwrite mfuuid ole32)

add_executable(cambridge_frame_ipc_probe
  tests/frame_ipc_probe.cpp
)
target_link_libraries(cambridge_frame_ipc_probe PRIVATE cambridge_frame_ipc)

add_executable(cambridge_media_source_tests
  tests/media_source_tests.cpp
)
target_include_directories(cambridge_media_source_tests PRIVATE include virtual-camera)
target_compile_definitions(cambridge_media_source_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cambridge_media_source_tests PRIVATE cambridge_frame_ipc mf mfplat mfuuid ole32)

enable_testing()
add_test(NAME cambridge_frame_ipc_tests COMMAND cambridge_frame_ipc_tests)
add_test(NAME cambridge_media_source_tests
  COMMAND cambridge_media_source_tests $<TARGET_FILE:cambridge_media_source>)
