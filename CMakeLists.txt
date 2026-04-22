cmake_minimum_required(VERSION 3.21)

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON)

project(DataTransfer LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(DATASET_DIR "${CMAKE_SOURCE_DIR}/DataSet")

if(NOT CONFIGURATION)
    set(CONFIGURATION ${CMAKE_BUILD_TYPE})
endif()

find_package(FFMPEG REQUIRED)
find_package(LibDataChannel CONFIG REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

set(COMMON_SRC_FILES
    src/base64.cpp
    src/data_channel_session.cpp
    src/input_command.cpp
    src/tcp_signaling_channel.cpp
    src/webrtc_video_session.cpp
)

set(COMMON_HEADER_FILES
    src/base64.h
    src/data_channel_session.h
    src/input_command.h
    src/tcp_signaling_channel.h
    src/webrtc_video_session.h
)

add_library(datatransfer_common STATIC
    ${COMMON_SRC_FILES}
    ${COMMON_HEADER_FILES}
)

target_link_libraries(datatransfer_common
    PUBLIC LibDataChannel::LibDataChannel
)

target_include_directories(datatransfer_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(datatransfer_common PUBLIC ${FFMPEG_LIBRARY_DIRS})

add_executable(lan_datachannel_server
    src/server_main.cpp
)

target_link_libraries(lan_datachannel_server
    PRIVATE datatransfer_common
)

add_executable(lan_datachannel_client
    src/client_main.cpp
)

target_link_libraries(lan_datachannel_client
    PRIVATE datatransfer_common
)

add_executable(video_stream_server
    src/ffmpeg_h264_file_source.cpp
    src/ffmpeg_h264_file_source.h
    src/video_stream_server_main.cpp
)

target_include_directories(video_stream_server PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(video_stream_server PRIVATE ${FFMPEG_LIBRARY_DIRS})

target_link_libraries(video_stream_server
    PRIVATE
        datatransfer_common
        ${FFMPEG_LIBRARIES}
        Qt5::Core
        Qt5::Network
)

add_executable(qt_video_client
    src/ffmpeg_h264_decoder.cpp
    src/ffmpeg_h264_decoder.h
    src/qt_video_client_main.cpp
)

target_include_directories(qt_video_client PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(qt_video_client PRIVATE ${FFMPEG_LIBRARY_DIRS})

target_link_libraries(qt_video_client
    PRIVATE
        datatransfer_common
        ${FFMPEG_LIBRARIES}
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Network
)

if(WIN32)
    target_link_libraries(datatransfer_common PUBLIC ws2_32)
    target_link_libraries(video_stream_server PRIVATE ws2_32)
endif()

enable_testing()
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(datatransfer_unit_tests
    tests/data_channel_session_tests.cpp
    tests/input_command_tests.cpp
    tests/video_frame_protocol_tests.cpp
    tests/webrtc_video_session_tests.cpp
    src/video_frame_protocol.cpp
    src/video_frame_protocol.h
)

target_link_libraries(datatransfer_unit_tests
    PRIVATE
        datatransfer_common
        GTest::gtest
        GTest::gtest_main
        Qt5::Core
)

gtest_discover_tests(datatransfer_unit_tests)
