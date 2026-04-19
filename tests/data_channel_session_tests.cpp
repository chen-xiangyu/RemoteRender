#include "data_channel_session.h"
#include "input_command.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace datatransfer {
namespace {

TEST(DataChannelSessionTests, InitialSessionIsNotOpen) {
    DataChannelSession session(false);
    EXPECT_FALSE(session.IsOpen());
}

TEST(DataChannelSessionTests, SendingBeforeChannelOpensThrows) {
    DataChannelSession session(false);
    MouseMovePayload payload;
    payload.x = 1;
    payload.y = 2;

    EXPECT_THROW(session.SendCommand(MakeMouseMoveCommand(1, 2ULL, payload)), std::runtime_error);
}

TEST(DataChannelSessionTests, UnsupportedSignalLineIsRejected) {
    DataChannelSession session(false);
    EXPECT_THROW(session.HandleSignalLine("BAD|payload"), std::runtime_error);
}

TEST(DataChannelSessionTests, IncompleteSdpSignalLineIsRejected) {
    DataChannelSession session(false);
    EXPECT_THROW(session.HandleSignalLine("SDP|only-type"), std::runtime_error);
}

TEST(DataChannelSessionTests, IncompleteCandidateSignalLineIsRejected) {
    DataChannelSession session(false);
    EXPECT_THROW(session.HandleSignalLine("CAND|only-mid"), std::runtime_error);
}

TEST(DataChannelSessionTests, InvalidBase64InsideSdpIsRejected) {
    DataChannelSession session(false);
    EXPECT_THROW(session.HandleSignalLine("SDP|%%%|%%%"), std::runtime_error);
}

} // namespace
} // namespace datatransfer
