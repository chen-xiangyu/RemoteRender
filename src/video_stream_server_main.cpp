#include "ffmpeg_h264_file_source.h"
#include "tcp_signaling_channel.h"
#include "webrtc_video_session.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "usage: video_stream_server <h264-video-file> [signaling-port]" << std::endl;
            return 1;
        }

        const std::string filePath = argv[1];
        const int signalingPort = argc >= 3 ? std::stoi(argv[2]) : 10000;

        datatransfer::FfmpegH264FileSource source(filePath);
        datatransfer::TcpSignalingChannel signaling;
        signaling.StartServer(signalingPort);

        datatransfer::WebRtcVideoSession session(true);
        signaling.StartReceiveLoop([&session](const std::string& line) {
            session.HandleSignalLine(line);
        });
        session.SetSignalSender([&signaling](const std::string& line) {
            signaling.SendLine(line);
        });
        session.Start();

        std::cout << "waiting for WebRTC video track to open..." << std::endl;
        session.WaitForTrackOpen();
        std::cout << "video track open, streaming H.264 samples" << std::endl;

        std::vector<std::uint8_t> sample;
        std::uint64_t sampleTimestampUs = 0;
        std::uint64_t previousTimestampUs = 0;
        bool firstFrame = true;

        while (source.ReadNextSample(sample, sampleTimestampUs)) {
            if (!firstFrame && sampleTimestampUs > previousTimestampUs) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(sampleTimestampUs - previousTimestampUs));
            }
            firstFrame = false;
            previousTimestampUs = sampleTimestampUs;

            session.SendVideoSample(sample, sampleTimestampUs);
        }

        signaling.Close();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "server error: " << ex.what() << std::endl;
        return 1;
    }
}
