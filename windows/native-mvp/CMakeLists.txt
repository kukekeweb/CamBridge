cmake_minimum_required(VERSION 3.21)

project(CamBridgeNativeMvp LANGUAGES CXX)

option(CAMBRIDGE_ENABLE_LIBDATACHANNEL
       "Build the opt-in libdatachannel native receiver adapter" OFF)

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
target_link_libraries(cambridge_frame_ipc PUBLIC kernel32 advapi32)

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

add_executable(cambridge_media_source_layout_tests
  tests/media_source_layout_tests.cpp
  virtual-camera/cambridge_media_source.cpp
  virtual-camera/diagnostic_log.cpp
)
target_include_directories(cambridge_media_source_layout_tests PRIVATE include virtual-camera)
target_compile_definitions(cambridge_media_source_layout_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cambridge_media_source_layout_tests PRIVATE cambridge_frame_ipc mf mfplat mfuuid mfsensorgroup shlwapi ole32 advapi32)

add_executable(cambridge_allocator_identity_tests
  tests/allocator_identity_tests.cpp
)
target_compile_definitions(cambridge_allocator_identity_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cambridge_allocator_identity_tests PRIVATE mfplat mfuuid ole32)

add_executable(cambridge_diagnostic_event_trace_tests
  tests/diagnostic_event_trace_tests.cpp
  virtual-camera/diagnostic_log.cpp
)
target_include_directories(cambridge_diagnostic_event_trace_tests PRIVATE virtual-camera)
target_compile_definitions(cambridge_diagnostic_event_trace_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(cambridge_diagnostic_event_trace_tests PRIVATE mfplat mfuuid ole32 shlwapi)

add_library(cambridge_receiver_core STATIC
  receiver/receiver_session.cpp
)
target_include_directories(cambridge_receiver_core PUBLIC receiver)
target_compile_features(cambridge_receiver_core PUBLIC cxx_std_20)

add_library(cambridge_latest_frame_publisher STATIC
  receiver/latest_frame_publisher.cpp
)
target_include_directories(cambridge_latest_frame_publisher PUBLIC include receiver)
target_compile_features(cambridge_latest_frame_publisher PUBLIC cxx_std_20)
target_link_libraries(cambridge_latest_frame_publisher PUBLIC cambridge_frame_ipc)

add_library(cambridge_h264_decoder STATIC
  receiver/h264_decoder.cpp
)
target_include_directories(cambridge_h264_decoder PUBLIC include receiver)
target_compile_features(cambridge_h264_decoder PUBLIC cxx_std_20)
target_link_libraries(cambridge_h264_decoder PUBLIC cambridge_frame_ipc mf mfplat mfuuid)

add_library(cambridge_receiver_media_pipeline STATIC
  receiver/receiver_media_pipeline.cpp
)
target_include_directories(cambridge_receiver_media_pipeline PUBLIC include receiver)
target_compile_features(cambridge_receiver_media_pipeline PUBLIC cxx_std_20)
target_link_libraries(cambridge_receiver_media_pipeline PUBLIC cambridge_h264_decoder
  cambridge_latest_frame_publisher)

add_executable(cambridge_receiver_session_tests
  tests/receiver_session_tests.cpp
)
target_link_libraries(cambridge_receiver_session_tests PRIVATE cambridge_receiver_core)

add_executable(cambridge_latest_frame_publisher_tests
  tests/latest_frame_publisher_tests.cpp
)
target_link_libraries(cambridge_latest_frame_publisher_tests PRIVATE cambridge_latest_frame_publisher)

add_executable(cambridge_h264_decoder_tests
  tests/h264_decoder_tests.cpp
)
target_link_libraries(cambridge_h264_decoder_tests PRIVATE cambridge_h264_decoder)

add_executable(cambridge_h264_decoder_probe
  tests/h264_decoder_probe.cpp
)
target_link_libraries(cambridge_h264_decoder_probe PRIVATE cambridge_h264_decoder)

add_executable(cambridge_receiver_media_pipeline_tests
  tests/receiver_media_pipeline_tests.cpp
)
target_include_directories(cambridge_receiver_media_pipeline_tests PRIVATE include receiver)
target_link_libraries(cambridge_receiver_media_pipeline_tests PRIVATE cambridge_receiver_media_pipeline)

if(CAMBRIDGE_ENABLE_LIBDATACHANNEL)
  find_package(LibDataChannel CONFIG REQUIRED)

  add_library(cambridge_libdatachannel_receiver STATIC
    receiver/libdatachannel_receiver.cpp
    receiver/native_signaling_session.cpp
    receiver/native_signaling_websocket.cpp
    receiver/signaling_protocol.cpp
  )
  target_include_directories(cambridge_libdatachannel_receiver PUBLIC receiver)
  target_compile_features(cambridge_libdatachannel_receiver PUBLIC cxx_std_20)
  find_package(nlohmann_json CONFIG REQUIRED)
  target_link_libraries(cambridge_libdatachannel_receiver
    PUBLIC cambridge_receiver_core LibDataChannel::LibDataChannel
           nlohmann_json::nlohmann_json)

  add_executable(cambridge_libdatachannel_receiver_tests
    tests/libdatachannel_receiver_tests.cpp
  )
  target_link_libraries(cambridge_libdatachannel_receiver_tests
    PRIVATE cambridge_libdatachannel_receiver)

  add_executable(cambridge_libdatachannel_loopback_tests
    tests/libdatachannel_loopback_tests.cpp
  )
  target_link_libraries(cambridge_libdatachannel_loopback_tests
    PRIVATE cambridge_libdatachannel_receiver iphlpapi ws2_32)

  add_executable(cambridge_libdatachannel_pipeline_loopback_tests
    tests/libdatachannel_pipeline_loopback_tests.cpp
  )
  target_link_libraries(cambridge_libdatachannel_pipeline_loopback_tests
    PRIVATE cambridge_libdatachannel_receiver cambridge_receiver_media_pipeline
            iphlpapi ws2_32)

  add_executable(cambridge_signaling_protocol_tests
    tests/signaling_protocol_tests.cpp
  )
  target_link_libraries(cambridge_signaling_protocol_tests
    PRIVATE cambridge_libdatachannel_receiver)

  add_executable(cambridge_native_signaling_session_tests
    tests/native_signaling_session_tests.cpp
  )
  target_link_libraries(cambridge_native_signaling_session_tests
    PRIVATE cambridge_libdatachannel_receiver)

  add_executable(cambridge_native_signaling_websocket_tests
    tests/native_signaling_websocket_tests.cpp
  )
  target_link_libraries(cambridge_native_signaling_websocket_tests
    PRIVATE cambridge_libdatachannel_receiver)

  add_executable(cambridge_native_receiver
    receiver/native_receiver_main.cpp
  )
  target_link_libraries(cambridge_native_receiver
    PRIVATE cambridge_libdatachannel_receiver cambridge_receiver_media_pipeline)

  add_executable(cambridge_h264_depacketizer_tests
    tests/h264_depacketizer_tests.cpp
  )
  target_link_libraries(cambridge_h264_depacketizer_tests
    PRIVATE LibDataChannel::LibDataChannel)
endif()

enable_testing()
add_test(NAME cambridge_frame_ipc_tests COMMAND cambridge_frame_ipc_tests)
add_test(NAME cambridge_media_source_tests
  COMMAND cambridge_media_source_tests $<TARGET_FILE:cambridge_media_source>)
add_test(NAME cambridge_media_source_layout_tests
  COMMAND cambridge_media_source_layout_tests)
add_test(NAME cambridge_media_source_tests_provided_allocator
  COMMAND cambridge_media_source_tests $<TARGET_FILE:cambridge_media_source> --provided-allocator)
add_test(NAME cambridge_media_source_tests_portrait
  COMMAND cambridge_media_source_tests $<TARGET_FILE:cambridge_media_source> --portrait)
add_test(NAME cambridge_allocator_identity_tests COMMAND cambridge_allocator_identity_tests)
add_test(NAME cambridge_diagnostic_event_trace_tests COMMAND cambridge_diagnostic_event_trace_tests)
add_test(NAME cambridge_receiver_session_tests COMMAND cambridge_receiver_session_tests)
add_test(NAME cambridge_latest_frame_publisher_tests COMMAND cambridge_latest_frame_publisher_tests)
add_test(NAME cambridge_h264_decoder_tests COMMAND cambridge_h264_decoder_tests)
add_test(NAME cambridge_receiver_media_pipeline_tests COMMAND cambridge_receiver_media_pipeline_tests)
if(CAMBRIDGE_ENABLE_LIBDATACHANNEL)
  add_test(NAME cambridge_libdatachannel_receiver_tests
    COMMAND cambridge_libdatachannel_receiver_tests)
  add_test(NAME cambridge_libdatachannel_loopback_tests
    COMMAND cambridge_libdatachannel_loopback_tests)
  add_test(NAME cambridge_libdatachannel_pipeline_loopback_tests
    COMMAND cambridge_libdatachannel_pipeline_loopback_tests)
  add_test(NAME cambridge_signaling_protocol_tests
    COMMAND cambridge_signaling_protocol_tests)
  add_test(NAME cambridge_native_signaling_session_tests
    COMMAND cambridge_native_signaling_session_tests)
  add_test(NAME cambridge_native_signaling_websocket_tests
    COMMAND cambridge_native_signaling_websocket_tests)
  add_test(NAME cambridge_h264_depacketizer_tests
    COMMAND cambridge_h264_depacketizer_tests)
endif()
