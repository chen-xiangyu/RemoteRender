#include "input_command.h"

#include <gtest/gtest.h>

#include <vector>

namespace datatransfer {
namespace {

TEST(InputCommandTests, MouseMoveRoundTripPreservesFields) {
    MouseMovePayload payload;
    payload.x = 640;
    payload.y = 360;
    payload.deltaX = 12;
    payload.deltaY = -7;

    const auto source = MakeMouseMoveCommand(17, 123456789ULL, payload);
    const auto bytes = SerializeCommand(source);
    const auto restored = DeserializeCommand(bytes);
    const auto decoded = DeserializeMouseMovePayload(restored);

    EXPECT_EQ(restored.version, 1);
    EXPECT_EQ(restored.type, InputCommandType::MouseMove);
    EXPECT_EQ(restored.sequence, 17U);
    EXPECT_EQ(restored.timestampUs, 123456789ULL);
    EXPECT_EQ(decoded.x, payload.x);
    EXPECT_EQ(decoded.y, payload.y);
    EXPECT_EQ(decoded.deltaX, payload.deltaX);
    EXPECT_EQ(decoded.deltaY, payload.deltaY);
}

TEST(InputCommandTests, MouseButtonRoundTripPreservesFields) {
    MouseButtonPayload payload;
    payload.button = MouseButton::Right;
    payload.state = ButtonState::Release;
    payload.x = 1024;
    payload.y = 768;

    const auto source = MakeMouseButtonCommand(8, 42ULL, payload);
    const auto restored = DeserializeCommand(SerializeCommand(source));
    const auto decoded = DeserializeMouseButtonPayload(restored);

    EXPECT_EQ(restored.type, InputCommandType::MouseButton);
    EXPECT_EQ(decoded.button, MouseButton::Right);
    EXPECT_EQ(decoded.state, ButtonState::Release);
    EXPECT_EQ(decoded.x, 1024);
    EXPECT_EQ(decoded.y, 768);
}

TEST(InputCommandTests, KeyRoundTripPreservesFields) {
    KeyPayload payload;
    payload.keyCode = 65;
    payload.state = ButtonState::Press;
    payload.modifiers = 0x3;

    const auto source = MakeKeyCommand(99, 777ULL, payload);
    const auto restored = DeserializeCommand(SerializeCommand(source));
    const auto decoded = DeserializeKeyPayload(restored);

    EXPECT_EQ(restored.type, InputCommandType::Key);
    EXPECT_EQ(decoded.keyCode, 65U);
    EXPECT_EQ(decoded.state, ButtonState::Press);
    EXPECT_EQ(decoded.modifiers, 0x3U);
}

TEST(InputCommandTests, CameraTransformRoundTripPreservesFields) {
    CameraTransformPayload payload;
    payload.yawDelta = 1.25F;
    payload.pitchDelta = -0.75F;
    payload.rollDelta = 0.5F;
    payload.zoomDelta = 2.0F;

    const auto source = MakeCameraTransformCommand(101, 9999ULL, payload);
    const auto restored = DeserializeCommand(SerializeCommand(source));
    const auto decoded = DeserializeCameraTransformPayload(restored);

    EXPECT_EQ(restored.type, InputCommandType::CameraTransform);
    EXPECT_FLOAT_EQ(decoded.yawDelta, 1.25F);
    EXPECT_FLOAT_EQ(decoded.pitchDelta, -0.75F);
    EXPECT_FLOAT_EQ(decoded.rollDelta, 0.5F);
    EXPECT_FLOAT_EQ(decoded.zoomDelta, 2.0F);
}

TEST(InputCommandTests, InvalidMagicIsRejected) {
    std::vector<std::uint8_t> bytes(24U, 0U);
    EXPECT_THROW(DeserializeCommand(bytes), std::runtime_error);
}

TEST(InputCommandTests, CommandTooShortIsRejected) {
    const std::vector<std::uint8_t> bytes(23U, 0U);
    EXPECT_THROW(DeserializeCommand(bytes), std::runtime_error);
}

TEST(InputCommandTests, PayloadSizeMismatchIsRejected) {
    MouseWheelPayload payload;
    payload.deltaX = 1;
    payload.deltaY = 2;

    auto bytes = SerializeCommand(MakeMouseWheelCommand(3, 5ULL, payload));
    bytes.pop_back();

    EXPECT_THROW(DeserializeCommand(bytes), std::runtime_error);
}

TEST(InputCommandTests, WrongPayloadTypeFailsSpecificDecoder) {
    MouseWheelPayload payload;
    payload.deltaX = 20;
    payload.deltaY = -40;

    const auto command = DeserializeCommand(SerializeCommand(MakeMouseWheelCommand(10, 20ULL, payload)));

    EXPECT_THROW(DeserializeKeyPayload(command), std::runtime_error);
}

TEST(InputCommandTests, SpecificDecoderRejectsUnexpectedPayloadSize) {
    InputCommand command;
    command.type = InputCommandType::MouseMove;
    command.payload = {1U, 2U, 3U};

    EXPECT_THROW(DeserializeMouseMovePayload(command), std::runtime_error);
}

TEST(InputCommandTests, DisplayStringContainsKeyFields) {
    MouseWheelPayload payload;
    payload.deltaX = 0;
    payload.deltaY = 120;

    const auto command = MakeMouseWheelCommand(11, 22ULL, payload);
    const auto text = ToDisplayString(command);

    EXPECT_NE(text.find("seq=11"), std::string::npos);
    EXPECT_NE(text.find("MouseWheel"), std::string::npos);
    EXPECT_NE(text.find("dy=120"), std::string::npos);
}

TEST(InputCommandTests, DisplayStringFallsBackForUnknownCommandType) {
    InputCommand command;
    command.type = static_cast<InputCommandType>(99);
    command.sequence = 5;
    command.timestampUs = 10;
    command.payload = {1U, 2U, 3U, 4U};

    const auto text = ToDisplayString(command);

    EXPECT_NE(text.find("seq=5"), std::string::npos);
    EXPECT_NE(text.find("Unknown"), std::string::npos);
    EXPECT_NE(text.find("payloadBytes=4"), std::string::npos);
}

} // namespace
} // namespace datatransfer
