#include "video_frame_protocol.h"

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QTcpSocket>
#include <QVBoxLayout>
#include <QWidget>

#include <iostream>
#include <stdexcept>

namespace {

class VideoClientWindow : public QWidget {
public:
    VideoClientWindow(const QString& host, quint16 port) {
        setWindowTitle("Qt Video Client");
        resize(960, 600);

        auto* layout = new QVBoxLayout(this);
        m_statusLabel = new QLabel("Connecting...", this);
        m_statusLabel->setAlignment(Qt::AlignCenter);

        m_videoLabel = new QLabel("Waiting for video stream", this);
        m_videoLabel->setAlignment(Qt::AlignCenter);
        m_videoLabel->setMinimumSize(640, 360);
        m_videoLabel->setStyleSheet("background-color: black; color: white;");

        layout->addWidget(m_statusLabel);
        layout->addWidget(m_videoLabel, 1);

        connect(&m_socket, &QTcpSocket::connected, this, [this]() {
            m_statusLabel->setText("Connected");
        });

        connect(&m_socket, &QTcpSocket::readyRead, this, [this]() {
            m_buffer.append(m_socket.readAll());
            ProcessBuffer();
        });

        connect(&m_socket, &QTcpSocket::disconnected, this, [this]() {
            m_statusLabel->setText("Disconnected");
        });

        connect(
            &m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this,
            [this](QAbstractSocket::SocketError) {
                m_statusLabel->setText("Socket error: " + m_socket.errorString());
            });

        m_socket.connectToHost(host, port);
    }

private:
    void ProcessBuffer() {
        while (true) {
            if (m_buffer.size() < datatransfer::kVideoFrameHeaderSize) {
                return;
            }

            const auto header = datatransfer::TryParseVideoFrameHeader(m_buffer);
            if (!header) {
                return;
            }

            const int packetSize =
                datatransfer::kVideoFrameHeaderSize + static_cast<int>(header->payloadSize);
            if (m_buffer.size() < packetSize) {
                return;
            }

            const QByteArray jpegBytes = m_buffer.mid(
                datatransfer::kVideoFrameHeaderSize,
                static_cast<int>(header->payloadSize));
            m_buffer.remove(0, packetSize);

            QImage image;
            if (!image.loadFromData(jpegBytes, "JPG")) {
                m_statusLabel->setText("Failed to decode JPEG frame");
                continue;
            }

            const QPixmap pixmap = QPixmap::fromImage(image);
            m_videoLabel->setPixmap(
                pixmap.scaled(m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_statusLabel->setText(
                QString("Frame %1  %2x%3")
                    .arg(header->frameIndex)
                    .arg(header->width)
                    .arg(header->height));
        }
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (const QPixmap* pixmap = m_videoLabel->pixmap()) {
            m_videoLabel->setPixmap(
                pixmap->scaled(m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

    QTcpSocket m_socket;
    QByteArray m_buffer;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_videoLabel = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    const QString host = argc >= 2 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("127.0.0.1");
    const quint16 port = argc >= 3 ? static_cast<quint16>(QString::fromLocal8Bit(argv[2]).toUShort()) : 10000;

    try {
        VideoClientWindow window(host, port);
        window.show();
        return app.exec();
    } catch (const std::exception& ex) {
        std::cerr << "video client error: " << ex.what() << std::endl;
        return 1;
    }
}
