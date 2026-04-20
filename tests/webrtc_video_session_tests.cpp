#include "webrtc_video_session.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace datatransfer {
namespace {

class SignalLineCollector {
public:
    void Push(const std::string& line) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lines.push_back(line);
        }
        m_condition.notify_all();
    }

    std::optional<std::string> WaitForPrefix(
        const std::string& prefix,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool ready = m_condition.wait_for(lock, timeout, [&]() {
            for (const auto& line : m_lines) {
                if (line.rfind(prefix, 0) == 0) {
                    return true;
                }
            }
            return false;
        });

        if (!ready) {
            return std::nullopt;
        }

        for (const auto& line : m_lines) {
            if (line.rfind(prefix, 0) == 0) {
                return line;
            }
        }

        return std::nullopt;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<std::string> m_lines;
};

TEST(WebRtcVideoSessionTests, InitialTrackIsNotOpen) {
    WebRtcVideoSession session(false);
    EXPECT_FALSE(session.IsTrackOpen());
}

TEST(WebRtcVideoSessionTests, SendingBeforeTrackOpensThrows) {
    WebRtcVideoSession session(true);
    EXPECT_THROW(session.SendVideoSample({0x00, 0x00, 0x00, 0x01}, 1000ULL), std::runtime_error);
}

TEST(WebRtcVideoSessionTests, StartEmitsLocalSdpSignalLine) {
    WebRtcVideoSession session(true);
    SignalLineCollector collector;

    session.SetSignalSender([&collector](const std::string& line) {
        collector.Push(line);
    });

    session.Start();

    const auto sdpLine = collector.WaitForPrefix("SDP|");
    ASSERT_TRUE(sdpLine.has_value());
    EXPECT_EQ(sdpLine->rfind("SDP|", 0), 0U);

    session.Close();
}

TEST(WebRtcVideoSessionTests, ReceiverAcceptsOfferAndEmitsAnswerSignalLine) {
    WebRtcVideoSession sender(true);
    WebRtcVideoSession receiver(false);
    SignalLineCollector senderSignals;
    SignalLineCollector receiverSignals;

    sender.SetSignalSender([&senderSignals](const std::string& line) {
        senderSignals.Push(line);
    });
    receiver.SetSignalSender([&receiverSignals](const std::string& line) {
        receiverSignals.Push(line);
    });

    sender.Start();

    const auto offerLine = senderSignals.WaitForPrefix("SDP|");
    ASSERT_TRUE(offerLine.has_value());

    EXPECT_NO_THROW(receiver.HandleSignalLine(*offerLine));

    const auto answerLine = receiverSignals.WaitForPrefix("SDP|");
    ASSERT_TRUE(answerLine.has_value());
    EXPECT_EQ(answerLine->rfind("SDP|", 0), 0U);

    sender.Close();
    receiver.Close();
}

TEST(WebRtcVideoSessionTests, UnsupportedSignalLineIsRejected) {
    WebRtcVideoSession session(false);
    EXPECT_THROW(session.HandleSignalLine("BAD|payload"), std::runtime_error);
}

TEST(WebRtcVideoSessionTests, IncompleteSdpSignalLineIsRejected) {
    WebRtcVideoSession session(false);
    EXPECT_THROW(session.HandleSignalLine("SDP|only-type"), std::runtime_error);
}

TEST(WebRtcVideoSessionTests, IncompleteCandidateSignalLineIsRejected) {
    WebRtcVideoSession session(false);
    EXPECT_THROW(session.HandleSignalLine("CAND|only-mid"), std::runtime_error);
}

TEST(WebRtcVideoSessionTests, InvalidBase64InsideSdpIsRejected) {
    WebRtcVideoSession session(false);
    EXPECT_THROW(session.HandleSignalLine("SDP|%%%|%%%"), std::runtime_error);
}

TEST(WebRtcVideoSessionTests, InvalidBase64InsideCandidateIsRejected) {
    WebRtcVideoSession session(false);
    EXPECT_THROW(session.HandleSignalLine("CAND|%%%|%%%"), std::runtime_error);
}

TEST(WebRtcVideoSessionTests, SendingAfterCloseIsRejected) {
    WebRtcVideoSession session(true);
    session.Close();
    EXPECT_FALSE(session.IsTrackOpen());
    EXPECT_THROW(session.SendVideoSample({0x00, 0x00, 0x00, 0x01}, 2000ULL), std::runtime_error);
}

} // namespace
} // namespace datatransfer
