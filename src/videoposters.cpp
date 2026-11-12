#include "videoposters.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMediaPlayer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVideoFrame>
#include <QVideoSink>

VideoPosters::VideoPosters(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(10 * 1000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(QVideoFrame()); });
}

VideoPosters::~VideoPosters() = default;

QString VideoPosters::posterPath(const QString &videoFile)
{
    const QFileInfo fi(videoFile);
    const QByteArray key = fi.absoluteFilePath().toUtf8() + '\n' + QByteArray::number(fi.size()) + '\n'
                           + QByteArray::number(fi.lastModified().toMSecsSinceEpoch());
    const QString name = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("posters/%1.jpg").arg(name));
}

QUrl VideoPosters::poster(const QUrl &video)
{
    if (!video.isLocalFile())
        return {};
    const QString file = video.toLocalFile();
    if (!QFileInfo(file).isFile())
        return {};
    const QString path = posterPath(file);
    if (QFileInfo::exists(path))
        return QUrl::fromLocalFile(path);
    if (!m_failed.contains(path) && m_current != file && !m_queue.contains(file)) {
        m_queue << file;
        if (m_current.isEmpty())
            QTimer::singleShot(0, this, &VideoPosters::next);
    }
    return {};
}

// One video after the other: a decoder for each at once would cost more
// than the posters save
void VideoPosters::next()
{
    if (!m_current.isEmpty() || m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_sink = new QVideoSink(this);
        m_player->setVideoSink(m_sink); // no audio output: silent
        connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (!m_current.isEmpty() && frame.isValid())
                finish(frame);
        });
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this] { finish(QVideoFrame()); });
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::InvalidMedia)
                finish(QVideoFrame());
        });
    }
    m_timeout.start();
    m_player->setSource(QUrl::fromLocalFile(m_current));
    m_player->play();
}

void VideoPosters::finish(const QVideoFrame &frame)
{
    if (m_current.isEmpty())
        return;
    m_timeout.stop();
    const QString file = std::exchange(m_current, QString());
    const QString path = posterPath(file);
    QImage image = frame.isValid() ? frame.toImage() : QImage();
    bool saved = false;
    if (!image.isNull()) {
        if (std::max(image.width(), image.height()) > kMaxSide)
            image = image.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile out(path);
        saved = out.open(QIODevice::WriteOnly) && image.convertToFormat(QImage::Format_RGB32).save(&out, "JPG", 85)
                && out.commit();
    }
    if (!saved)
        m_failed.insert(path);
    // Stopping from inside the sink's signal is fine; the next video waits a turn
    QTimer::singleShot(0, this, [this] {
        if (m_player) {
            m_player->stop();
            m_player->setSource(QUrl());
        }
        next();
    });
    if (saved) {
        ++m_revision;
        emit revisionChanged();
        emit posterReady(QUrl::fromLocalFile(file));
    }
}
