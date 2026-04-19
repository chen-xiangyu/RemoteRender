#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace datatransfer {

enum class InputCommandType : std::uint16_t {
    MouseMove = 1,
    MouseButton = 2,
    MouseWheel = 3,
    Key = 4,
    CameraTransform = 5
};

enum class ButtonState : std::uint8_t {
    Release = 0,
    Press = 1
};

enum class MouseButton : std::uint8_t {
    Left = 1,
    Right = 2,
    Middle = 3
};

struct InputCommand {
    std::uint16_t version = 1;
    InputCommandType type = InputCommandType::MouseMove;
    std::uint32_t sequence = 0;
    std::uint64_t timestampUs = 0;
    std::vector<std::uint8_t> payload;
};

struct MouseMovePayload {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t deltaX = 0;
    std::int32_t deltaY = 0;
};

struct MouseButtonPayload {
    MouseButton button = MouseButton::Left;
    ButtonState state = ButtonState::Press;
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct MouseWheelPayload {
    std::int32_t deltaX = 0;
    std::int32_t deltaY = 0;
};

struct KeyPayload {
    std::uint32_t keyCode = 0;
    ButtonState state = ButtonState::Press;
    std::uint8_t modifiers = 0;
};

struct CameraTransformPayload {
    float yawDelta = 0.0F;
    float pitchDelta = 0.0F;
    float rollDelta = 0.0F;
    float zoomDelta = 0.0F;
};

std::vector<std::uint8_t> SerializeCommand(const InputCommand& command);
InputCommand DeserializeCommand(const std::vector<std::uint8_t>& bytes);

InputCommand MakeMouseMoveCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const MouseMovePayload& payload);
InputCommand MakeMouseButtonCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const MouseButtonPayload& payload);
InputCommand MakeMouseWheelCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const MouseWheelPayload& payload);
InputCommand MakeKeyCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const KeyPayload& payload);
InputCommand MakeCameraTransformCommand(
    std::uint32_t sequence,
    std::uint64_t timestampUs,
    const CameraTransformPayload& payload);

MouseMovePayload DeserializeMouseMovePayload(const InputCommand& command);
MouseButtonPayload DeserializeMouseButtonPayload(const InputCommand& command);
MouseWheelPayload DeserializeMouseWheelPayload(const InputCommand& command);
KeyPayload DeserializeKeyPayload(const InputCommand& command);
CameraTransformPayload DeserializeCameraTransformPayload(const InputCommand& command);

std::string ToDisplayString(const InputCommand& command);

} // namespace datatransfer
