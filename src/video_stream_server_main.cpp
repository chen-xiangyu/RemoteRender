#include "ffmpeg_video_reader.h"
#include "video_frame_protocol.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <iostream>
#include <memory>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "usage: video_stream_server <video-file> [port]" << std::endl;
        return 1;
    }

    const QString videoPath = QString::fromLocal8Bit(argv[1]);
    const quint16 port = argc >= 3 ? static_cast<quint16>(QString::fromLocal8Bit(argv[2]).toUShort()) : 10000;

    std::unique_ptr<datatransfer::FfmpegVideoReader> reader;
    try {
        reader = std::make_unique<datatransfer::FfmpegVideoReader>(videoPath.toStdString());
    } catch (const std::exception& ex) {
        std::cerr << "failed to open video: " << ex.what() << std::endl;
        return 1;
    }

    QTcpServer server;
    QTimer frameTimer;
    frameTimer.setInterval(reader->FrameIntervalMs());

    QTcpSocket* activeClient = nullptr;
    std::uint32_t frameIndex = 0;

    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        while (server.hasPendingConnections()) {
            if (activeClient) {
                auto* extraClient = server.nextPendingConnection();
                extraClient->disconnectFromHost();
                extraClient->deleteLater();
                continue;
            }

            activeClient = server.nextPendingConnection();
            std::cout << "client connected for video streaming" << std::endl;

            QObject::connect(activeClient, &QTcpSocket::disconnected, [&]() {
                std::cout << "client disconnected" << std::endl;
                activeClient->deleteLater();
                activeClient = nullptr;
                frameTimer.stop();
            });

            frameTimer.start();
        }
    });

    QObject::connect(&frameTimer, &QTimer::timeout, [&]() {
        if (!activeClient || activeClient->state() != QAbstractSocket::ConnectedState) {
            frameTimer.stop();
            return;
        }

        QImage frame;
        if (!reader->ReadNextFrame(frame)) {
            std::cerr << "failed to read next frame" << std::endl;
            frameTimer.stop();
            return;
        }

        QByteArray jpegBytes;
        QBuffer buffer(&jpegBytes);
        buffer.open(QIODevice::WriteOnly);
        if (!frame.save(&buffer, "JPG", 75)) {
            std::cerr << "failed to encode jpeg frame" << std::endl;
            frameTimer.stop();
            return;
        }

        datatransfer::VideoFramePacketHeader header;
        header.payloadSize = static_cast<std::uint32_t>(jpegBytes.size());
        header.frameIndex = frameIndex++;
        header.width = static_cast<std::uint32_t>(frame.width());
        header.height = static_cast<std::uint32_t>(frame.height());
        header.timestampMs = static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());

        const QByteArray packet = datatransfer::SerializeVideoFramePacket(header, jpegBytes);
        const qint64 written = activeClient->write(packet);
        if (written != packet.size()) {
            std::cerr << "short write while sending video frame" << std::endl;
        }
        activeClient->flush();
    });

    if (!server.listen(QHostAddress::Any, port)) {
        std::cerr << "failed to listen on port " << port << std::endl;
        return 1;
    }

    std::cout << "video server listening on port " << port << std::endl;
    std::cout << "stream source: " << videoPath.toStdString() << std::endl;
    return app.exec();
}
