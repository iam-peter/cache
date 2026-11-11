#include "highlight.h"
#include "markdown.h"

#include <QColor>
#include <QTest>

class TestMarkdown : public QObject
{
    Q_OBJECT

private slots:
    void tasks()
    {
        const QString md = QStringLiteral("# List\n- [ ] one\n  - [x] two\n1. [ ] three\n```\n- [ ] in code\n```\n* [X] four");
        const QList<Markdown::Task> tasks = Markdown::tasks(md);
        QCOMPARE(tasks.size(), 4);
        QCOMPARE(tasks.at(0).text, QStringLiteral("one"));
        QVERIFY(!tasks.at(0).done);
        QVERIFY(tasks.at(1).done);
        QCOMPARE(tasks.at(1).indent, 2);
        QCOMPARE(tasks.at(2).line, 3);
        QCOMPARE(tasks.at(3).text, QStringLiteral("four"));
    }

    void toggleTask()
    {
        const QString md = QStringLiteral("- [ ] one\n```\n- [ ] code\n```\n- [x] two");
        QCOMPARE(Markdown::toggleTask(md, 0), QStringLiteral("- [x] one\n```\n- [ ] code\n```\n- [x] two"));
        QCOMPARE(Markdown::toggleTask(md, 1), QStringLiteral("- [ ] one\n```\n- [ ] code\n```\n- [ ] two"));
        QCOMPARE(Markdown::toggleTask(md, 5), md);
    }

    void wikiLinks()
    {
        const QString md = QStringLiteral("See [[Release plan]] and [[Beta/Ideas|the ideas]].\n"
                                          "![[Meeting with Sam]]\n"
                                          "`[[not a link]]`\n"
                                          "```\n[[in code]]\n```");
        const QList<Markdown::WikiLink> links = Markdown::wikiLinks(md);
        QCOMPARE(links.size(), 3);
        QCOMPARE(links.at(0).target, QStringLiteral("Release plan"));
        QVERIFY(!links.at(0).standalone);
        QCOMPARE(links.at(1).target, QStringLiteral("Beta/Ideas"));
        QCOMPARE(links.at(1).alias, QStringLiteral("the ideas"));
        QVERIFY(links.at(2).embed);
        QVERIFY(links.at(2).standalone);
        QCOMPARE(links.at(2).line, 1);
    }

    void linkify()
    {
        QCOMPARE(Markdown::linkify(QStringLiteral("a [[Big plan|plan]] b `[[x]]`")),
                 QStringLiteral("a [plan](note:Big%20plan) b `[[x]]`"));
        QCOMPARE(Markdown::unlinkify(QStringLiteral("a [[Big plan|plan]] and [[Other]]")),
                 QStringLiteral("a plan and Other"));
    }

    void setEmbed()
    {
        const QString md = QStringLiteral("x\n[[Note]]\ny");
        const QString embedded = Markdown::setEmbed(md, 1, true);
        QCOMPARE(embedded, QStringLiteral("x\n![[Note]]\ny"));
        QCOMPARE(Markdown::setEmbed(embedded, 1, false), md);
        QCOMPARE(Markdown::setEmbed(embedded, 1, true), embedded);
    }

    void fences()
    {
        const QString md = QStringLiteral("text\n```cpp\nint x = 1;\nreturn x;\n```\nmore\n~~~\nplain\n~~~");
        const QList<Markdown::Fence> fences = Markdown::fences(md);
        QCOMPARE(fences.size(), 2);
        QCOMPARE(fences.at(0).language, QStringLiteral("cpp"));
        QCOMPARE(fences.at(0).code, QStringLiteral("int x = 1;\nreturn x;"));
        QCOMPARE(fences.at(0).startLine, 1);
        QCOMPARE(fences.at(0).endLine, 4);
        QCOMPARE(fences.at(1).code, QStringLiteral("plain"));
    }

    void tableFromCells()
    {
        QCOMPARE(Markdown::tableFromCells(QStringLiteral("Name\tCount\r\nApples\t3\r\nPipes | ok\t4\r\n")),
                 QStringLiteral("| Name | Count |\n|---|---|\n| Apples | 3 |\n| Pipes \\| ok | 4 |"));
        // Not a grid: one row, one column, ragged rows
        QVERIFY(Markdown::tableFromCells(QStringLiteral("a\tb")).isEmpty());
        QVERIFY(Markdown::tableFromCells(QStringLiteral("a\nb")).isEmpty());
        QVERIFY(Markdown::tableFromCells(QStringLiteral("a\tb\nc")).isEmpty());
        // CSV, with quoted cells
        QCOMPARE(Markdown::tableFromCells(QStringLiteral("x,y\n\"1,5\",\"say \"\"hi\"\"\"\n"), QLatin1Char(',')),
                 QStringLiteral("| x | y |\n|---|---|\n| 1,5 | say \"hi\" |"));
    }

    void webUrls()
    {
        QVERIFY(Markdown::isWebUrl(QStringLiteral("https://doc.qt.io/qt-6/ ")));
        QVERIFY(Markdown::isWebUrl(QStringLiteral("http://example.com/a?b=c")));
        QVERIFY(!Markdown::isWebUrl(QStringLiteral("see https://doc.qt.io")));
        QVERIFY(!Markdown::isWebUrl(QStringLiteral("file:///tmp/x")));
        QVERIFY(!Markdown::isWebUrl(QStringLiteral("doc.qt.io")));
        QCOMPARE(Markdown::link(QStringLiteral("A [b]"), QStringLiteral("https://x.org/a b")),
                 QStringLiteral("[A (b)](https://x.org/a%20b)"));
    }

    void language_data()
    {
        QTest::addColumn<QString>("fence");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("expected");
        QTest::newRow("alias") << "c++" << "" << "cpp";
        QTest::newRow("ts") << "ts" << "" << "javascript";
        QTest::newRow("bash") << "bash" << "" << "shell";
        QTest::newRow("guess cpp") << "" << "#include <vector>\nint main() {}" << "cpp";
        QTest::newRow("guess python") << "" << "def run(x):\n    return x" << "python";
        QTest::newRow("guess sql") << "" << "SELECT * FROM notes;" << "sql";
        QTest::newRow("guess json") << "" << "{\n  \"a\": 1\n}" << "json";
        QTest::newRow("nothing") << "" << "just some words" << "text";
    }
    void language()
    {
        QFETCH(QString, fence);
        QFETCH(QString, code);
        QFETCH(QString, expected);
        QCOMPARE(Highlight::language(fence, code), expected);
    }

    void spans()
    {
        const QString code = QStringLiteral("// hi\nint x = foo(\"s\", 42); /* c */");
        QList<Highlight::Kind> kinds;
        QStringList texts;
        for (const Highlight::Span &s : Highlight::spans(code, QStringLiteral("cpp"))) {
            kinds << s.kind;
            texts << code.mid(s.start, s.length);
        }
        QCOMPARE(texts, (QStringList{"// hi", "int", "foo", "\"s\"", "42", "/* c */"}));
        QCOMPARE(kinds.at(0), Highlight::Kind::Comment);
        QCOMPARE(kinds.at(1), Highlight::Kind::Type);
        QCOMPARE(kinds.at(2), Highlight::Kind::Function);
        QCOMPARE(kinds.at(3), Highlight::Kind::String);
        QCOMPARE(kinds.at(4), Highlight::Kind::Number);
    }

    void pythonStrings()
    {
        const QString code = QStringLiteral("x = \"\"\"a\nb\"\"\" # note");
        const QList<Highlight::Span> spans = Highlight::spans(code, QStringLiteral("python"));
        QCOMPARE(spans.size(), 2);
        QCOMPARE(code.mid(spans.at(0).start, spans.at(0).length), QStringLiteral("\"\"\"a\nb\"\"\""));
        QCOMPARE(spans.at(1).kind, Highlight::Kind::Comment);
    }

    void toHtml()
    {
        const QString html = Highlight::toHtml(QStringLiteral("if (a < b) return;"), QStringLiteral("cpp"),
                                               {{QStringLiteral("keyword"), QColor(Qt::red)}});
        QVERIFY(html.contains(QStringLiteral("<span style=\"color:#ff0000;\">if</span>")));
        QVERIFY(html.contains(QStringLiteral("&lt;")));
    }
};

QTEST_APPLESS_MAIN(TestMarkdown)
#include "tst_markdown.moc"
