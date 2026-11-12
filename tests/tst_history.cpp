#include "history.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>

class TestHistory : public QObject
{
    Q_OBJECT

private:
    static void write(const QString &path, const QByteArray &text)
    {
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }
    static QByteArray read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void commitAndRestore()
    {
        History history;
        if (!history.available())
            QSKIP("git is not installed");
        QTemporaryDir root;
        const QString note = root.filePath("P/notes/a.md");
        write(note, "first");
        QVERIFY(history.commit(root.path(), "P", "one"));
        write(note, "second");
        write(root.filePath("P/notes/b.md"), "added later");
        QVERIFY(history.commit(root.path(), "P", "two"));
        // Nothing changed: no new version
        QVERIFY(history.commit(root.path(), "P", "three"));

        const QVariantList versions = history.versions(root.path(), "P");
        QCOMPARE(versions.size(), 2);
        QCOMPARE(versions.first().toMap().value("subject").toString(), QStringLiteral("two"));

        QVERIFY(history.restore(root.path(), "P", versions.last().toMap().value("sha").toString()));
        QCOMPARE(read(note), QByteArray("first"));
        QVERIFY(!QFile::exists(root.filePath("P/notes/b.md"))); // added later: gone
        // The restore is a version of its own, so it can be undone
        QCOMPARE(history.versions(root.path(), "P").size(), 3);
    }

    // A scheduled version is committed in the background; asking for the
    // versions waits for it
    void commitsInTheBackground()
    {
        History history;
        if (!history.available())
            QSKIP("git is not installed");
        QTemporaryDir root;
        write(root.filePath("P/notes/a.md"), "first");
        history.schedule(root.path(), "P");
        QSignalSpy committed(&history, &History::committed);
        history.flush(); // returns before git is done
        QCOMPARE(history.versions(root.path(), "P").size(), 1);
        QTRY_COMPARE(committed.count(), 1); // emitted on the git thread
        QCOMPARE(committed.first().first().toString(), QStringLiteral("P"));
    }

    void ownRepositoryInsideAnother()
    {
        History history;
        if (!history.available())
            QSKIP("git is not installed");
        QTemporaryDir outer;
        QVERIFY(QProcess::execute("git", {"-C", outer.path(), "init", "-q"}) == 0);
        const QString root = outer.filePath("workspace");
        write(root + "/P/notes/a.md", "x");
        QVERIFY(history.commit(root, "P", "one"));
        QVERIFY(QFile::exists(root + "/.git"));
    }
};

QTEST_GUILESS_MAIN(TestHistory)
#include "tst_history.moc"
