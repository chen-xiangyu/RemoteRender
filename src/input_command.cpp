#include "input_command.h"

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace datatransfer {

namespace {

constexpr std::uint32_t kMagic = 0x31444D43U; // CMD1
constexpr std::size_t kHeaderSize = 24U;

void AppendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void AppendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void AppendU64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void AppendI32(std::vector<std::uint8_t>& output, std::int32_t value) {
    AppendU32(output, static_cast<std::uint32_t>(value));
}

void AppendF32(std::vector<std::uint8_t>& output, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(output, bits);
}

std::uint16_t ReadU16(const std::vector<std::uint8_t>& input, std::size_t offset) {
    return static_cast<std::uint16_t>(input.at(offset)) |
           (static_cast<std::uint16_t>(input.at(offset + 1U)) << 8U);
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& input, std::size_t offset) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input.at(offset + static_cast<std::size_t>(index))) << (index * 8);
    }
    return value;
}

std::uint64_t ReadU64(const std::vector<std::uint8_t>& input, std::size_t offset) {
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input.at(offset + static_cast<std::size_t>(index))) << (index * 8);
    }
    return value;
}

std::int32_t ReadI32(const std::vector<std::uint8_t>& input, std::size_t offset) {
    return static_cast<std::int32_t>(ReadU32(input, offset));
}

float ReadF32(const std::vector<std::uint8_t>& input, std::size_t offset) {
    const std::uint32_t bits = ReadU32(input, offset);
    float value = 0.0F;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void EnsureType(const InputCommand& command, InputCommandType expected) {
    if (command.type != expected) {
        throw std::runtime_error("Unexpected command type");
    }
}

template <typename Func>
InputCommand MakeCommand(
    InputCommandType type,
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    Func&& fillPayload) {
    InputCommand command;
    command.type = type;
    command.sequence = sequence;
    command.timestampUs = timestampUs;
    fillPayload(command.payload);
    return command;
}

} // namespace

std::vector<std::uint8_t> SerializeCommand(const InputCommand& command) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderSize + command.payload.size());

    AppendU32(bytes, kMagic);
    AppendU16(bytes, command.version);
    AppendU16(bytes, static_cast<std::uint16_t>(command.type));
    AppendU32(bytes, command.sequence);
    AppendU64(bytes, command.timestampUs);
    AppendU32(bytes, static_cast<std::uint32_t>(command.payload.size()));
    bytes.insert(bytes.end(), command.payload.begin(), command.payload.end());

    return bytes;
}

InputCommand DeserializeCommand(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kHeaderSize) {
        throw std::runtime_error("Command too short");
    }

    if (ReadU32(bytes, 0) != kMagic) {
        throw std::runtime_error("Invalid command magic");
    }

    InputCommand command;
    command.version = ReadU16(bytes, 4);
    command.type = static_cast<InputCommandType>(ReadU16(bytes, 6));
    command.sequence = ReadU32(bytes, 8);
    command.timestampUs = ReadU64(bytes, 12);
    const auto payloadSize = static_cast<std::size_t>(ReadU32(bytes, 20));
    const std::size_t payloadOffset = 24U;

    if (bytes.size() != payloadOffset + payloadSize) {
        throw std::runtime_error("Command payload size mismatch");
    }

    command.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(payloadOffset), bytes.end());
    return command;
}

InputCommand MakeMouseMoveCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const MouseMovePayload& payload) {
    return MakeCommand(InputCommandType::MouseMove, sequence, timestampUs, [&](std::vector<std::uint8_t>& out) {
        AppendI32(out, payload.x);
        AppendI32(out, payload.y);
        AppendI32(out, payload.deltaX);
        AppendI32(out, payload.deltaY);
    });
}

InputCommand MakeMouseButtonCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const MouseButtonPayload& payload) {
    return MakeCommand(InputCommandType::MouseButton, sequence, timestampUs, [&](std::vector<std::uint8_t>& out) {
        out.push_back(static_cast<std::uint8_t>(payload.button));
        out.push_back(static_cast<std::uint8_t>(payload.state));
        out.push_back(0);
        out.push_back(0);
        AppendI32(out, payload.x);
        AppendI32(out, payload.y);
    });
}

InputCommand MakeMouseWheelCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const MouseWheelPayload& payload) {
    return MakeCommand(InputCommandType::MouseWheel, sequence, timestampUs, [&](std::vector<std::uint8_t>& out) {
        AppendI32(out, payload.deltaX);
        AppendI32(out, payload.deltaY);
    });
}

InputCommand MakeKeyCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const KeyPayload& payload) {
    return MakeCommand(InputCommandType::Key, sequence, timestampUs, [&](std::vector<std::uint8_t>& out) {
        AppendU32(out, payload.keyCode);
        out.push_back(static_cast<std::uint8_t>(payload.state));
        out.push_back(payload.modifiers);
        out.push_back(0);
        out.push_back(0);
    });
}

InputCommand MakeCameraTransformCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const CameraTransformPayload& payload) {
    return MakeCommand(InputCommandType::CameraTransform, sequence, timestampUs, [&](std::vector<std::uint8_t>& out) {
        AppendF32(out, payload.yawDelta);
        AppendF32(out, payload.pitchDelta);
        AppendF32(out, payload.rollDelta);
        AppendF32(out, payload.zoomDelta);
    });
}

MouseMovePayload DeserializeMouseMovePayload(const InputCommand& command) {
    EnsureType(command, InputCommandType::MouseMove);
    if (command.payload.size() != 16U) {
        throw std::runtime_error("Invalid mouse move payload size");
    }

    MouseMovePayload payload;
    payload.x = ReadI32(command.payload, 0);
    payload.y = ReadI32(command.payload, 4);
    payload.deltaX = ReadI32(command.payload, 8);
    payload.deltaY = ReadI32(command.payload, 12);
    return payload;
}

MouseButtonPayload DeserializeMouseButtonPayload(const InputCommand& command) {
    EnsureType(command, InputCommandType::MouseButton);
    if (command.payload.size() != 12U) {
        throw std::runtime_error("Invalid mouse button payload size");
    }

    MouseButtonPayload payload;
    payload.button = static_cast<MouseButton>(command.payload[0]);
    payload.state = static_cast<ButtonState>(command.payload[1]);
    payload.x = ReadI32(command.payload, 4);
    payload.y = ReadI32(command.payload, 8);
    return payload;
}

MouseWheelPayload DeserializeMouseWheelPayload(const InputCommand& command) {
    EnsureType(command, InputCommandType::MouseWheel);
    if (command.payload.size() != 8U) {
        throw std::runtime_error("Invalid mouse wheel payload size");
    }

    MouseWheelPayload payload;
    payload.deltaX = ReadI32(command.payload, 0);
    payload.deltaY = ReadI32(command.payload, 4);
    return payload;
}

KeyPayload DeserializeKeyPayload(const InputCommand& command) {
    EnsureType(command, InputCommandType::Key);
    if (command.payload.size() != 8U) {
        throw std::runtime_error("Invalid key payload size");
    }

    KeyPayload payload;
    payload.keyCode = ReadU32(command.payload, 0);
    payload.state = static_cast<ButtonState>(command.payload[4]);
    payload.modifiers = command.payload[5];
    return payload;
}

CameraTransformPayload DeserializeCameraTransformPayload(const InputCommand& command) {
    EnsureType(command, InputCommandType::CameraTransform);
    if (command.payload.size() != 16U) {
        throw std::runtime_error("Invalid camera transform payload size");
    }

    CameraTransformPayload payload;
    payload.yawDelta = ReadF32(command.payload, 0);
    payload.pitchDelta = ReadF32(command.payload, 4);
    payload.rollDelta = ReadF32(command.payload, 8);
    payload.zoomDelta = ReadF32(command.payload, 12);
    return payload;
}

std::string ToDisplayString(const InputCommand& command) {
    std::ostringstream stream;
    stream << "seq=" << command.sequence << " tsUs=" << command.timestampUs << " ";

    switch (command.type) {
    case InputCommandType::MouseMove: {
        const auto payload = DeserializeMouseMovePayload(command);
        stream << "MouseMove x=" << payload.x
               << " y=" << payload.y
               << " dx=" << payload.deltaX
               << " dy=" << payload.deltaY;
        break;
    }
    case InputCommandType::MouseButton: {
        const auto payload = DeserializeMouseButtonPayload(command);
        stream << "MouseButton button=" << static_cast<int>(payload.button)
               << " state=" << static_cast<int>(payload.state)
               << " x=" << payload.x
               << " y=" << payload.y;
        break;
    }
    case InputCommandType::MouseWheel: {
        const auto payload = DeserializeMouseWheelPayload(command);
        stream << "MouseWheel dx=" << payload.deltaX
               << " dy=" << payload.deltaY;
        break;
    }
    case InputCommandType::Key: {
        const auto payload = DeserializeKeyPayload(command);
        stream << "Key code=" << payload.keyCode
               << " state=" << static_cast<int>(payload.state)
               << " modifiers=" << static_cast<int>(payload.modifiers);
        break;
    }
    case InputCommandType::CameraTransform: {
        const auto payload = DeserializeCameraTransformPayload(command);
        stream << "Camera yaw=" << payload.yawDelta
               << " pitch=" << payload.pitchDelta
               << " roll=" << payload.rollDelta
               << " zoom=" << payload.zoomDelta;
        break;
    }
    default:
        stream << "Unknown payloadBytes=" << command.payload.size();
        break;
    }

    return stream.str();
}

} // namespace datatransfer
