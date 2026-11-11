#include "boardmodel.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QBuffer>
#include <QImage>
#include <QTest>

// Serves /page (an HTML page with a title) and /pic.png on localhost
class TinyServer : public QTcpServer
{
public:
    TinyServer()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    const QByteArray request = socket->readAll();
                    QByteArray body, type = "text/html";
                    if (request.startsWith("GET /pic.png")) {
                        QImage image(8, 6, QImage::Format_RGB32);
                        image.fill(Qt::red);
                        QBuffer buffer(&body);
                        buffer.open(QIODevice::WriteOnly);
                        image.save(&buffer, "PNG");
                        type = "image/png";
                    } else {
                        body = "<html><head><title>Hello &amp; welcome</title></head><body>x</body></html>";
                    }
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString url(const char *path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(QLatin1String(path)); }
};

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

    void richTextFromOtherApps()
    {
        BoardModel board;
        board.load(project("html"));
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        const QString html = QStringLiteral("<p>Hello <b>bold</b> and <a href=\"https://qt.io\">a link</a></p>"
                                            "<ul><li>one</li><li>two</li></ul>"
                                            "<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>"
                                            "<p><img src=\"data:image/png;base64,%1\"></p>").arg(QString::fromLatin1(png.toBase64()));
        const QStringList ids = board.dropData({{"text/html", html.toUtf8()}, {"text/plain", "Hello bold"}}, 0, 0);
        QCOMPARE(ids.size(), 1);
        const QString md = board.getById(ids.first()).value("text").toString();
        QVERIFY2(md.contains(QStringLiteral("**bold**")), qPrintable(md));
        QVERIFY2(md.contains(QStringLiteral("[a link](https://qt.io)")), qPrintable(md));
        QVERIFY2(md.contains(QStringLiteral("- one")), qPrintable(md));
        QVERIFY2(md.contains(QStringLiteral("|A|B|")) || md.contains(QStringLiteral("| A | B |")), qPrintable(md));
        QVERIFY2(md.contains(QStringLiteral("](assets/paste-")), qPrintable(md)); // the image, saved
    }

    void spreadsheetCells()
    {
        BoardModel board;
        board.load(project("cells"));
        // Spreadsheets also offer a picture of the cells: the table wins
        QImage image(4, 4, QImage::Format_RGB32);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        const QStringList ids = board.dropData({{"text/plain", "Item\tCost\nTea\t3\n"}, {"image/png", png}}, 0, 0);
        QCOMPARE(board.getById(ids.first()).value("text").toString().trimmed(),
                 QStringLiteral("| Item | Cost |\n|---|---|\n| Tea | 3 |"));
    }

    void filesOfAnyKind()
    {
        const QString path = project("files");
        BoardModel board;
        board.load(path);
        const QString pdf = m_dir.filePath("report.pdf");
        QFile f(pdf);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("%PDF-1.4 fake");
        f.close();
        const QString csv = m_dir.filePath("sales.csv");
        QFile c(csv);
        QVERIFY(c.open(QIODevice::WriteOnly));
        c.write("Month,Total\nJan,10\nFeb,12\n");
        c.close();
        const int row = board.importUrls({QUrl::fromLocalFile(pdf), QUrl::fromLocalFile(csv)}, 0, 0);
        QVERIFY(row >= 0);
        QCOMPARE(board.count(), 2); // the table a note of its own, the PDF in another
        QString attachment, table;
        for (int i = 0; i < board.count(); ++i) {
            const QString text = board.get(i).value("text").toString();
            (text.contains(QStringLiteral("report.pdf")) ? attachment : table) = text;
        }
        QCOMPARE(attachment.trimmed(), QStringLiteral("[report.pdf](assets/report.pdf)"));
        QCOMPARE(BoardModel::titleForText(attachment), QStringLiteral("report.pdf"));
        QCOMPARE(BoardModel::titleForText(table), QStringLiteral("sales"));
        QCOMPARE(BoardModel::titleForText(QStringLiteral("| Region | Q1 |\n|---|---|\n| N | 1 |")),
                 QStringLiteral("Region · Q1"));
        QCOMPARE(BoardModel::titleForText(QStringLiteral("See [[Plan]] and [the docs](https://x.org)")),
                 QStringLiteral("See Plan and the docs"));
        QVERIFY(QFile::exists(path + QStringLiteral("/assets/report.pdf")));
        QVERIFY(table.contains(QStringLiteral("| Jan | 10 |")));
        const QVariantMap file = board.renderSegments(attachment, 300, 14, {}).first().toMap();
        QCOMPARE(file.value("kind").toString(), QStringLiteral("file"));
        QCOMPARE(file.value("extension").toString(), QStringLiteral("PDF"));
        QVERIFY(file.value("size").toString().contains(QStringLiteral("bytes")));
        // Into a note being edited: the same Markdown
        QCOMPARE(board.filesMarkdown({QUrl::fromLocalFile(pdf)}), QStringLiteral("[report.pdf](assets/report.pdf)"));
    }

    void webLinksAndImages()
    {
        TinyServer server;
        BoardModel board;
        board.load(project("web"));
        // A link: titled once the page is fetched
        const QString page = server.url("/page");
        QStringList ids = board.dropData({{"text/uri-list", (page + "\r\n").toUtf8()}}, 0, 0);
        QCOMPARE(board.getById(ids.first()).value("text").toString().trimmed(), QStringLiteral("[%1](%1)").arg(page));
        QTRY_COMPARE(board.getById(ids.first()).value("text").toString().trimmed(),
                     QStringLiteral("[Hello & welcome](%1)").arg(page));
        // A link with a name in the drag keeps it
        ids = board.dropData({{"text/uri-list", page.toUtf8()},
                              {"text/html", QStringLiteral("<a href=\"%1\">Named</a>").arg(page).toUtf8()}}, 0, 0);
        QCOMPARE(board.getById(ids.first()).value("text").toString().trimmed(), QStringLiteral("[Named](%1)").arg(page));
        // An image: fetched into assets
        ids = board.dropData({{"text/uri-list", server.url("/pic.png").toUtf8()}}, 0, 0);
        QTRY_VERIFY(board.getById(ids.first()).value("text").toString().contains(QStringLiteral("](assets/pic.png)")));
        // One that can't be had: a link to it
        ids = board.dropData({{"text/plain", "http://127.0.0.1:1/gone.png"}}, 0, 0);
        QTRY_COMPARE(board.getById(ids.first()).value("text").toString().trimmed(),
                     QStringLiteral("[gone.png](http://127.0.0.1:1/gone.png)"));
    }

    void ownNotesPasteBackAsTheyAre()
    {
        BoardModel board;
        board.load(project("own"));
        const int row = board.addNote(0, 0, QStringLiteral("# Own\n\n- [ ] task with **bold**"));
        board.copyToClipboard(row);
        QCOMPARE(board.clipboardMarkdown(), QString()); // the editor pastes the text itself
        const QStringList ids = board.pasteFromClipboard(500, 0);
        QCOMPARE(board.getById(ids.first()).value("text").toString().trimmed(),
                 QStringLiteral("# Own\n\n- [ ] task with **bold**"));
    }

    void reverseConnection()
    {
        BoardModel board;
        board.load(project("reverse"));
        const QString a = board.get(board.addNote(0, 0, QStringLiteral("A"))).value("id").toString();
        const QString b = board.get(board.addNote(400, 0, QStringLiteral("B"))).value("id").toString();
        const QString ab = board.addLink(a, b);
        // B to A along the same route: the same connector, arrows both ways
        QCOMPARE(board.addLink(b, a), ab);
        QCOMPARE(board.linksVariant().size(), 1);
        QCOMPARE(board.linksVariant().first().toMap().value("arrow").toString(), QStringLiteral("both"));
        // Along other sides: a connector of its own
        const QString other = board.addLink(b, a, QStringLiteral("bottom"), QStringLiteral("left"));
        QVERIFY(!other.isEmpty() && other != ab);
        QCOMPARE(board.linksVariant().size(), 2);
        // Twice the same: still two
        QCOMPARE(board.addLink(b, a, QStringLiteral("bottom"), QStringLiteral("left")), other);
        QCOMPARE(board.linksVariant().size(), 2);
    }

    void loopsAndSides()
    {
        const QString path = project("loops");
        QString a, loop;
        {
            BoardModel board;
            board.load(path);
            a = board.get(board.addNote(0, 0, QStringLiteral("A"))).value("id").toString();
            loop = board.addLink(a, a, QStringLiteral("right"), QStringLiteral("top"));
            QVERIFY(!loop.isEmpty());
            QVERIFY(board.addLink(a, a, QStringLiteral("middle"), {}).isEmpty()); // no such side
            // A loop is no mindmap branch
            QCOMPARE(board.childrenOf(a), QStringList());
            QCOMPARE(board.parentOf(a), QString());
            QCOMPARE(board.data(board.index(0), BoardModel::ChildCountRole).toInt(), 0);
            board.save();
        }
        BoardModel board;
        board.load(path);
        const QVariantMap link = board.linksVariant().first().toMap();
        QCOMPARE(link.value("from").toString(), a);
        QCOMPARE(link.value("to").toString(), a);
        QCOMPARE(link.value("fromSide").toString(), QStringLiteral("right"));
        QCOMPARE(link.value("toSide").toString(), QStringLiteral("top"));
    }

    void moveAnEnd()
    {
        BoardModel board;
        board.load(project("relink"));
        const QString a = board.get(board.addNote(0, 0, QStringLiteral("A"))).value("id").toString();
        const QString b = board.get(board.addNote(400, 0, QStringLiteral("B"))).value("id").toString();
        const QString c = board.get(board.addNote(0, 400, QStringLiteral("C"))).value("id").toString();
        const QString link = board.addLink(a, b);
        board.setLink(link, {{"to", c}, {"toSide", "left"}, {"fromSide", "bottom"}});
        QVariantMap l = board.linksVariant().first().toMap();
        QCOMPARE(l.value("to").toString(), c);
        QCOMPARE(l.value("toSide").toString(), QStringLiteral("left"));
        QCOMPARE(board.childrenOf(a), QStringList{c});
        board.setLink(link, {{"to", "no such item"}});
        QCOMPARE(board.linksVariant().first().toMap().value("to").toString(), c);
        board.undo(); // the move of both ends was one step
        QCOMPARE(board.linksVariant().first().toMap().value("to").toString(), b);
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
        QVERIFY(mime->hasFormat(QStringLiteral("application/x-cache-items")));
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
