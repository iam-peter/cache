#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QMediaPlayer;
class QVideoFrame;
class QVideoSink;

// Still pictures of the videos in notes: their first frame, made once and
// kept in the cache folder. A note shows the picture and loads a player
// only to play, so panning and zooming past videos costs no more than
// past images.
class VideoPosters : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Posters)
    QML_SINGLETON

    // Goes up whenever a poster is ready: bindings that call poster() ask again
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit VideoPosters(QObject *parent = nullptr);
    ~VideoPosters() override;

    int revision() const { return m_revision; }

    // The poster of a local video file as a file URL, or empty while it is
    // being made (or when the video can't be read)
    Q_INVOKABLE QUrl poster(const QUrl &video);

    // Where the poster of `videoFile` goes: named after the file's path,
    // size and time, so a changed video gets a new one
    static QString posterPath(const QString &videoFile);

    // Longest side of a poster, in pixels: sharp in a note on a 2x screen
    static constexpr int kMaxSide = 960;

signals:
    void revisionChanged();
    void posterReady(const QUrl &video);

private:
    void next();
    void finish(const QVideoFrame &frame);

    QStringList m_queue;          // video files waiting for a poster
    QSet<QString> m_failed;       // no poster to be had
    QString m_current;
    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    QTimer m_timeout;
    int m_revision = 0;
};
