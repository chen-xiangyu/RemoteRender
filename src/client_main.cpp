#include "data_channel_session.h"
#include "input_command.h"
#include "tcp_signaling_channel.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> Split(const std::string& input) {
    std::istringstream stream(input);
    std::vector<std::string> parts;
    std::string part;
    while (stream >> part) {
        parts.push_back(part);
    }
    return parts;
}

std::uint64_t NowUs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::optional<datatransfer::InputCommand> ParseCommandLine(const std::string& line, std::uint32_t sequence) {
    const auto parts = Split(line);
    if (parts.empty()) {
        return std::nullopt;
    }

    const auto timestampUs = NowUs();

    if (parts[0] == "move" && parts.size() == 5U) {
        datatransfer::MouseMovePayload payload;
        payload.x = std::stoi(parts[1]);
        payload.y = std::stoi(parts[2]);
        payload.deltaX = std::stoi(parts[3]);
        payload.deltaY = std::stoi(parts[4]);
        return datatransfer::MakeMouseMoveCommand(sequence, timestampUs, payload);
    }

    if (parts[0] == "button" && parts.size() == 5U) {
        datatransfer::MouseButtonPayload payload;
        const std::string& buttonName = parts[1];
        if (buttonName == "left") {
            payload.button = datatransfer::MouseButton::Left;
        } else if (buttonName == "right") {
            payload.button = datatransfer::MouseButton::Right;
        } else if (buttonName == "middle") {
            payload.button = datatransfer::MouseButton::Middle;
        } else {
            throw std::runtime_error("Unsupported button name");
        }

        payload.state = (parts[2] == "down") ? datatransfer::ButtonState::Press : datatransfer::ButtonState::Release;
        payload.x = std::stoi(parts[3]);
        payload.y = std::stoi(parts[4]);
        return datatransfer::MakeMouseButtonCommand(sequence, timestampUs, payload);
    }

    if (parts[0] == "wheel" && parts.size() == 3U) {
        datatransfer::MouseWheelPayload payload;
        payload.deltaX = std::stoi(parts[1]);
        payload.deltaY = std::stoi(parts[2]);
        return datatransfer::MakeMouseWheelCommand(sequence, timestampUs, payload);
    }

    if (parts[0] == "key" && parts.size() >= 3U && parts.size() <= 4U) {
        datatransfer::KeyPayload payload;
        payload.keyCode = static_cast<std::uint32_t>(std::stoul(parts[1]));
        payload.state = (parts[2] == "down") ? datatransfer::ButtonState::Press : datatransfer::ButtonState::Release;
        payload.modifiers = parts.size() == 4U ? static_cast<std::uint8_t>(std::stoul(parts[3])) : 0U;
        return datatransfer::MakeKeyCommand(sequence, timestampUs, payload);
    }

    if (parts[0] == "camera" && parts.size() == 5U) {
        datatransfer::CameraTransformPayload payload;
        payload.yawDelta = std::stof(parts[1]);
        payload.pitchDelta = std::stof(parts[2]);
        payload.rollDelta = std::stof(parts[3]);
        payload.zoomDelta = std::stof(parts[4]);
        return datatransfer::MakeCameraTransformCommand(sequence, timestampUs, payload);
    }

    throw std::runtime_error("Unsupported command format");
}

void PrintUsage() {
    std::cout << "Input command examples:" << std::endl;
    std::cout << "  move <x> <y> <dx> <dy>" << std::endl;
    std::cout << "  button <left|right|middle> <down|up> <x> <y>" << std::endl;
    std::cout << "  wheel <dx> <dy>" << std::endl;
    std::cout << "  key <keyCode> <down|up> [modifiers]" << std::endl;
    std::cout << "  camera <yaw> <pitch> <roll> <zoom>" << std::endl;
    std::cout << "  quit" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "usage: lan_datachannel_client <server-ip> [signaling-port]" << std::endl;
            return 1;
        }

        const std::string serverIp = argv[1];
        const int signalingPort = argc >= 3 ? std::stoi(argv[2]) : 9000;

        datatransfer::TcpSignalingChannel signaling;
        signaling.ConnectTo(serverIp, signalingPort);

        datatransfer::DataChannelSession session(false);
        signaling.StartReceiveLoop([&session](const std::string& line) {
            session.HandleSignalLine(line);
        });

        session.SetSignalSender([&signaling](const std::string& line) {
            signaling.SendLine(line);
        });
        session.Start();

        PrintUsage();
        session.WaitForOpen();

        std::uint32_t sequence = 1;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "quit") {
                break;
            }

            if (line.empty()) {
                continue;
            }

            try {
                auto command = ParseCommandLine(line, sequence);
                if (command) {
                    session.SendCommand(*command);
                    std::cout << "[client-send] " << datatransfer::ToDisplayString(*command) << std::endl;
                    ++sequence;
                }
            } catch (const std::exception& ex) {
                std::cout << "invalid command: " << ex.what() << std::endl;
                PrintUsage();
            }
        }

        signaling.Close();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "client error: " << ex.what() << std::endl;
        return 1;
    }
}
