#include "videoposters.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Posters of videos: made from the first frame, cached, renewed when the
// video changes. tests/data/teal.mp4 is a second of solid teal frames.
class TestPosters : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // a cache folder of its own
        QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
    }

    void firstFrame()
    {
        QTemporaryDir dir;
        const QString video = dir.filePath("clip.mp4");
        QVERIFY(QFile::copy(QStringLiteral(CACHE_TEST_DATA "/teal.mp4"), video));
        const QUrl url = QUrl::fromLocalFile(video);

        VideoPosters posters;
        QSignalSpy ready(&posters, &VideoPosters::posterReady);
        QVERIFY(posters.poster(url).isEmpty()); // being made
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
        QCOMPARE(ready.first().first().toUrl(), url);

        const QUrl poster = posters.poster(url);
        QVERIFY(poster.isLocalFile());
        const QImage image(poster.toLocalFile());
        QCOMPARE(image.size(), QSize(160, 96));
        const QColor middle = image.pixelColor(80, 48);
        QVERIFY2(qAbs(middle.red() - 0x1d) < 24 && qAbs(middle.green() - 0x9e) < 24 && qAbs(middle.blue() - 0x75) < 24,
                 qPrintable(middle.name()));

        // Another start of the app: from the cache, right away
        VideoPosters again;
        QCOMPARE(again.poster(url), poster);

        // The video changed: a poster of its own
        QFile f(video);
        QVERIFY(f.open(QIODevice::Append));
        f.write(QByteArray(16, '\0'));
        f.close();
        QVERIFY(VideoPosters::posterPath(video) != poster.toLocalFile());
    }

    void notAVideo()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("broken.mp4");
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a video");
        f.close();
        VideoPosters posters;
        QVERIFY(posters.poster(QUrl::fromLocalFile(file)).isEmpty());
        // Given up on, without a poster, and not tried again
        QTest::qWait(3000);
        QVERIFY(posters.poster(QUrl::fromLocalFile(file)).isEmpty());
        QVERIFY(!QFile::exists(VideoPosters::posterPath(file)));
        QVERIFY(posters.poster(QUrl(QStringLiteral("https://example.com/a.mp4"))).isEmpty());
    }
};

QTEST_MAIN(TestPosters)
#include "tst_posters.moc"
