#include "ffmpeg_h264_decoder.h"
#include "tcp_signaling_channel.h"
#include "webrtc_video_session.h"

#include <QApplication>
#include <QLabel>
#include <QMetaObject>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {

class VideoClientWindow : public QWidget {
public:
    explicit VideoClientWindow(const std::string& host, int port)
        : m_host(host), m_port(port) {
        setWindowTitle("Qt WebRTC Video Client");
        resize(960, 600);

        auto* layout = new QVBoxLayout(this);
        m_statusLabel = new QLabel("Connecting...", this);
        m_statusLabel->setAlignment(Qt::AlignCenter);

        m_videoLabel = new QLabel("Waiting for WebRTC video track", this);
        m_videoLabel->setAlignment(Qt::AlignCenter);
        m_videoLabel->setMinimumSize(640, 360);
        m_videoLabel->setStyleSheet("background-color: black; color: white;");

        layout->addWidget(m_statusLabel);
        layout->addWidget(m_videoLabel, 1);

        m_decoder = std::make_unique<datatransfer::FfmpegH264Decoder>();
        m_signaling = std::make_unique<datatransfer::TcpSignalingChannel>();
        m_session = std::make_unique<datatransfer::WebRtcVideoSession>(false);

        m_session->SetSignalSender([this](const std::string& line) {
            m_signaling->SendLine(line);
        });

        m_session->SetVideoFrameReceiver([this](const std::vector<std::uint8_t>& frame, std::uint64_t timestampUs) {
            QImage image;
            if (!m_decoder->Decode(frame, image)) {
                return;
            }

            QMetaObject::invokeMethod(
                this,
                [this, image = std::move(image), timestampUs]() mutable {
                    const QPixmap pixmap = QPixmap::fromImage(image);
                    m_videoLabel->setPixmap(
                        pixmap.scaled(m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    m_statusLabel->setText(
                        QString("Receiving WebRTC H.264 stream  ts=%1 us")
                            .arg(static_cast<qulonglong>(timestampUs)));
                },
                Qt::QueuedConnection);
        });

        m_signaling->ConnectTo(host, port);
        m_signaling->StartReceiveLoop([this](const std::string& line) {
            m_session->HandleSignalLine(line);
        });

        m_statusLabel->setText(
            QString("Connected to signaling %1:%2, waiting for media").arg(QString::fromStdString(host)).arg(port));
    }

    ~VideoClientWindow() override {
        if (m_signaling) {
            m_signaling->Close();
        }
    }

private:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (const QPixmap* pixmap = m_videoLabel->pixmap()) {
            m_videoLabel->setPixmap(
                pixmap->scaled(m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

    std::string m_host;
    int m_port = 0;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_videoLabel = nullptr;
    std::unique_ptr<datatransfer::FfmpegH264Decoder> m_decoder;
    std::unique_ptr<datatransfer::TcpSignalingChannel> m_signaling;
    std::unique_ptr<datatransfer::WebRtcVideoSession> m_session;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    const std::string host = argc >= 2 ? argv[1] : "127.0.0.1";
    const int port = argc >= 3 ? std::stoi(argv[2]) : 10000;

    try {
        VideoClientWindow window(host, port);
        window.show();
        return app.exec();
    } catch (const std::exception& ex) {
        std::cerr << "video client error: " << ex.what() << std::endl;
        return 1;
    }
}
