#include "data_channel_session.h"
#include "input_command.h"
#include "tcp_signaling_channel.h"

#include <exception>
#include <iostream>
#include <string>

namespace {

int ParsePort(const char* text) {
    return text ? std::stoi(text) : 9000;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const int signalingPort = argc >= 2 ? ParsePort(argv[1]) : 9000;

        datatransfer::TcpSignalingChannel signaling;
        signaling.StartServer(signalingPort);

        datatransfer::DataChannelSession session(true);
        session.SetCommandReceiver([](const datatransfer::InputCommand& command) {
            std::cout << "[server-recv] " << datatransfer::ToDisplayString(command) << std::endl;
        });

        signaling.StartReceiveLoop([&session](const std::string& line) {
            session.HandleSignalLine(line);
        });

        session.SetSignalSender([&signaling](const std::string& line) {
            signaling.SendLine(line);
        });
        session.Start();

        std::cout << "Waiting for client input commands..." << std::endl;
        std::cout << "Type quit to exit." << std::endl;
        session.WaitForOpen();

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit") {
                break;
            }
        }

        signaling.Close();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "server error: " << ex.what() << std::endl;
        return 1;
    }
}
