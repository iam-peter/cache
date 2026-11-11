#include "boardmodel.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>

class TestBoard : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString project(const char *name)
    {
        const QString path = m_dir.filePath(QString::fromLatin1(name));
        QDir().mkpath(path);
        return path;
    }

private slots:
    void linksPersist()
    {
        const QString path = project("links");
        QString a, b;
        {
            BoardModel board;
            board.load(path);
            a = board.get(board.addNote(0, 0, QStringLiteral("# A"))).value("id").toString();
            b = board.get(board.addNote(400, 0, QStringLiteral("# B"))).value("id").toString();
            const QString link = board.addLink(a, b);
            QVERIFY(!link.isEmpty());
            board.setLink(link, {{QStringLiteral("label"), QStringLiteral("leads to")},
                                 {QStringLiteral("style"), QStringLiteral("elbow")}});
            board.addItem(QStringLiteral("shape"), 0, 300, 100, 60, QStringLiteral("diamond"), QStringLiteral("Why?"));
            board.save();
        }
        BoardModel board;
        board.load(path);
        QCOMPARE(board.count(), 3);
        QCOMPARE(board.linksVariant().size(), 1);
        const QVariantMap link = board.linksVariant().first().toMap();
        QCOMPARE(link.value("from").toString(), a);
        QCOMPARE(link.value("label").toString(), QStringLiteral("leads to"));
        QCOMPARE(link.value("style").toString(), QStringLiteral("elbow"));
        const int shape = board.indexOf(board.itemsIn(-10, 290, 200, 100).value(0));
        QCOMPARE(board.get(shape).value("shape").toString(), QStringLiteral("diamond"));
        QCOMPARE(board.get(shape).value("text").toString(), QStringLiteral("Why?"));
    }

    void removingAnItemRemovesItsLinks()
    {
        BoardModel board;
        board.load(project("remove"));
        const QString a = board.get(board.addNote(0, 0, QStringLiteral("A"))).value("id").toString();
        const QString b = board.get(board.addNote(400, 0, QStringLiteral("B"))).value("id").toString();
        board.addLink(a, b);
        board.removeItems({b});
        QCOMPARE(board.linksVariant().size(), 0);
        board.undo();
        QCOMPARE(board.count(), 2);
        QCOMPARE(board.linksVariant().size(), 1);
    }

    void mindmap()
    {
        BoardModel board;
        board.load(project("mindmap"));
        const QString root = board.get(board.addNote(0, 0, QStringLiteral("# Root"))).value("id").toString();
        const QString c1 = board.addChild(root);
        const QString c2 = board.addChild(root);
        const QString sibling = board.addSibling(c1);
        QCOMPARE(board.parentOf(c1), root);
        QCOMPARE(board.parentOf(sibling), root);
        // c1, then its new sibling right under it, then c2 moved down
        QCOMPARE(board.childrenOf(root), (QStringList{c1, sibling, c2}));
        QVERIFY(board.getById(c1).value("x").toDouble() > board.getById(root).value("w").toDouble());

        board.toggleCollapsed(root);
        QVERIFY(board.getById(c1).value("hidden").toBool());
        QCOMPARE(board.data(board.index(board.indexOf(root)), BoardModel::DescendantsRole).toInt(), 3);
        board.toggleCollapsed(root);
        QVERIFY(!board.getById(c1).value("hidden").toBool());
    }

    void tidy()
    {
        BoardModel board;
        board.load(project("tidy"));
        const QString root = board.get(board.addNote(0, 0, QStringLiteral("# Root"))).value("id").toString();
        QStringList kids;
        for (int i = 0; i < 3; ++i)
            kids << board.addChild(root);
        // Scatter them, then tidy
        board.moveItems({QVariantMap{{"id", kids[0]}, {"x", 900.0}, {"y", -700.0}},
                         QVariantMap{{"id", kids[2]}, {"x", -300.0}, {"y", 50.0}}});
        board.tidy(root);
        const double x0 = board.getById(kids[0]).value("x").toDouble();
        for (const QString &kid : kids)
            QCOMPARE(board.getById(kid).value("x").toDouble(), x0);
        const QVariantMap r = board.getById(root);
        QVERIFY(x0 >= r.value("x").toDouble() + r.value("w").toDouble());
        // Children keep their order (kids[0] was dragged to the top, kids[2]
        // above kids[1]), centred on the root and without overlapping
        const QStringList order = board.childrenOf(root);
        QCOMPARE(order, (QStringList{kids[0], kids[2], kids[1]}));
        const double top = board.getById(order.first()).value("y").toDouble();
        const QVariantMap last = board.getById(order.last());
        const double bottom = last.value("y").toDouble() + last.value("h").toDouble();
        QCOMPARE(qRound((top + bottom) / 2), qRound(r.value("y").toDouble() + r.value("h").toDouble() / 2));
        for (int i = 1; i < order.size(); ++i) {
            const QVariantMap above = board.getById(order.at(i - 1));
            QVERIFY(board.getById(order.at(i)).value("y").toDouble()
                    >= above.value("y").toDouble() + above.value("h").toDouble());
        }
    }

    void alignAndDistribute()
    {
        BoardModel board;
        board.load(project("align"));
        QStringList ids;
        for (double x : {0.0, 150.0, 1000.0})
            ids << board.get(board.addNote(x, x / 10, QStringLiteral("n"))).value("id").toString();
        board.align(ids, QStringLiteral("top"));
        for (const QString &id : ids)
            QCOMPARE(board.getById(id).value("y").toDouble(), 0.0);
        board.distribute(ids, true);
        const double w = board.getById(ids[0]).value("w").toDouble();
        const double gap1 = board.getById(ids[1]).value("x").toDouble() - w;
        const double gap2 = board.getById(ids[2]).value("x").toDouble() - board.getById(ids[1]).value("x").toDouble() - w;
        QCOMPARE(qRound(gap1), qRound(gap2));
        // One undo step each
        board.undo();
        QCOMPARE(board.getById(ids[1]).value("x").toDouble(), 150.0);
    }

    void tasks()
    {
        BoardModel board;
        board.load(project("tasks"));
        const int row = board.addNote(0, 0, QStringLiteral("- [ ] a\n- [ ] b"));
        board.toggleTask(row, 1);
        QCOMPARE(board.get(row).value("text").toString(), QStringLiteral("- [ ] a\n- [x] b"));
        const QVariantList segments = board.renderSegments(board.get(row).value("text").toString(), 300, 14, {});
        QCOMPARE(segments.size(), 1);
        const QString html = segments.first().toMap().value("html").toString();
        QVERIFY(html.contains(QStringLiteral("task:0")));
        QVERIFY(html.contains(QStringLiteral("task:1")));
        QVERIFY(html.contains(QStringLiteral("☑")));
    }

    void segments()
    {
        BoardModel board;
        board.load(project("segments"));
        const QString md = QStringLiteral("# T\n\n- [ ] first\n\n```cpp\nint x;\n```\n\n- [ ] second\n\n![[Other note]]\n\nSee [[Other note]].");
        const QVariantList segments = board.renderSegments(md, 300, 14, {});
        QStringList kinds;
        for (const QVariant &s : segments)
            kinds << s.toMap().value("kind").toString();
        QCOMPARE(kinds, (QStringList{"text", "code", "text", "embed", "text"}));
        // Task numbers carry on across the code block
        QVERIFY(segments.at(2).toMap().value("html").toString().contains(QStringLiteral("task:1")));
        QCOMPARE(segments.at(1).toMap().value("language").toString(), QStringLiteral("cpp"));
        QCOMPARE(segments.at(3).toMap().value("target").toString(), QStringLiteral("Other note"));
        QVERIFY(segments.at(3).toMap().value("expanded").toBool());
        QVERIFY(segments.at(4).toMap().value("html").toString().contains(QStringLiteral("note:Other%20note")));
    }

    void wikiEdges()
    {
        BoardModel board;
        board.load(project("wiki"));
        const QString a = board.get(board.addNote(0, 0, QStringLiteral("# Alpha\n\nSee [[Beta]]"))).value("id").toString();
        const QString b = board.get(board.addNote(0, 300, QStringLiteral("# Beta"))).value("id").toString();
        QCOMPARE(board.noteByTitle(QStringLiteral("beta")), b);
        QCOMPARE(board.wikiEdges().size(), 1);
        QCOMPARE(board.wikiEdges().first().toMap().value("from").toString(), a);
        QCOMPARE(board.wikiEdges().first().toMap().value("to").toString(), b);
    }

    void sections()
    {
        BoardModel board;
        board.load(project("sections"));
        const QString section = board.get(board.addItem(QStringLiteral("section"), 0, 0, 500, 400, {}, QStringLiteral("Backlog")))
                                    .value("id").toString();
        const int inside = board.addNote(50, 50, QStringLiteral("in"));
        board.addNote(800, 50, QStringLiteral("out"));
        QCOMPARE(board.contentsOf(section), QStringList{board.get(inside).value("id").toString()});
        QCOMPARE(board.data(board.index(inside), BoardModel::SectionTitleRole).toString(), QStringLiteral("Backlog"));
        // Sections lie under the rest: a click inside one finds the note
        QCOMPARE(board.itemAt(60, 60), inside);
    }

    void copyAndPasteItems()
    {
        BoardModel board;
        board.load(project("copy"));
        const QString a = board.get(board.addNote(0, 0, QStringLiteral("# A"))).value("id").toString();
        const QString b = board.get(board.addNote(400, 0, QStringLiteral("# B"))).value("id").toString();
        board.addLink(a, b);
        board.copyItems({a, b});
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime->hasFormat(QStringLiteral("application/x-notecanvas")));
        QVERIFY(mime->html().contains(QStringLiteral("A")));
        const QStringList pasted = board.pasteFromClipboard(1000, 1000);
        QCOMPARE(pasted.size(), 2);
        QCOMPARE(board.count(), 4);
        QCOMPARE(board.linksVariant().size(), 2);
        QCOMPARE(board.getById(pasted.first()).value("text").toString(), QStringLiteral("# A"));
    }

    void templates()
    {
        BoardModel board;
        board.load(project("templates"));
        const QVariantList list = board.templates({});
        QVERIFY(list.size() >= 7);
        const QString kanban = list.first().toMap().value("path").toString();
        const QStringList added = board.insertTemplate(kanban, 0, 0);
        QVERIFY(added.size() >= 4);
        QVERIFY(board.saveTemplate(added, QStringLiteral("Mine"), m_dir.path()));
        QCOMPARE(board.templates(m_dir.path()).size(), list.size() + 1);
    }

    void clipboardHtmlForTeams()
    {
        BoardModel board;
        board.load(project("clip"));
        const int row = board.addNote(0, 0, QStringLiteral("# T\n\n- [x] done\n\n```python\ndef f(): pass\n```\n\nSee [[Other]]"));
        board.copyToClipboard(row);
        const QString html = QGuiApplication::clipboard()->mimeData()->html();
        QVERIFY(html.contains(QStringLiteral("☑")));
        QVERIFY(!html.contains(QStringLiteral("task:")));
        QVERIFY(html.contains(QStringLiteral("<span style=\"color:#cf222e;\">def</span>")));
        QVERIFY(html.contains(QStringLiteral("Other")));
        QVERIFY(!html.contains(QStringLiteral("[[")));
    }
};

QTEST_MAIN(TestBoard)
#include "tst_board.moc"
