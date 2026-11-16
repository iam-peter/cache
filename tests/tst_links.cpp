#include "jiraresolver.h"
#include "links.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

// A stand-in for Jira on localhost: one issue, /myself, and the requests
// it was sent. Or, with `redirectTo`, only redirects there.
class FakeJira : public QTcpServer
{
public:
    explicit FakeJira(const QByteArray &authorization, const QString &redirectTo = {})
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this, authorization, redirectTo] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, authorization, redirectTo] {
                    const QByteArray request = socket->readAll();
                    requests << request;
                    QByteArray status = "200 OK", body, extra;
                    if (!redirectTo.isEmpty()) {
                        status = "302 Found";
                        extra = "Location: " + redirectTo.toUtf8() + "\r\n";
                    } else if (!request.contains("\r\nAuthorization: " + authorization + "\r\n")) {
                        status = "401 Unauthorized";
                        body = R"({"errorMessages":["You are not authenticated."]})";
                    } else if (request.startsWith("GET /rest/api/2/myself ")) {
                        body = R"({"displayName":"Ada Lovelace"})";
                    } else if (request.startsWith("GET /rest/api/2/issue/IDS5-7?")) {
                        body = R"({"key":"IDS5-7","fields":{
                            "summary":"Fix the crash on resize",
                            "status":{"name":"In Progress","statusCategory":{"key":"indeterminate"}},
                            "issuetype":{"name":"Bug"},"priority":{"name":"P1"},
                            "assignee":null,"reporter":{"displayName":"Grace Hopper"},
                            "updated":"2026-11-12T10:00:00.000+0100"}})";
                    } else {
                        status = "404 Not Found";
                        body = R"({"errorMessages":["Issue does not exist or you do not have permission to see it."]})";
                    }
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\n" + extra
                                  + "Content-Length: " + QByteArray::number(body.size())
                                  + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString host() const { return QStringLiteral("127.0.0.1:%1").arg(serverPort()); }
    QString issue(const char *key) const
    {
        return QStringLiteral("http://%1/browse/%2").arg(host(), QLatin1String(key));
    }
    QList<QByteArray> requests;
};

class TestLinks : public QObject
{
    Q_OBJECT

private slots:
    void hosts()
    {
        QCOMPARE(Links::hostOf(QStringLiteral("https://Example.atlassian.net/browse/X-1")),
                 QStringLiteral("example.atlassian.net"));
        QCOMPARE(Links::hostOf(QStringLiteral(" example.atlassian.net ")), QStringLiteral("example.atlassian.net"));
        QCOMPARE(Links::hostOf(QStringLiteral("jira.example.org:8443")), QStringLiteral("jira.example.org:8443"));
        QCOMPARE(Links::hostOf(QString()), QString());
    }

    void onlyHostsWithAnAccount()
    {
        Links links{Links::Detached{}};
        JiraResolver jira(&links);
        const QUrl issue(QStringLiteral("https://example.atlassian.net/browse/IDS5-7"));
        QVERIFY(!jira.handles(issue));
        links.accounts()->remember({QStringLiteral("jira"), QStringLiteral("example.atlassian.net"), {}}, "t");
        QVERIFY(jira.handles(issue));
        QVERIFY(jira.handles(QUrl(QStringLiteral("https://example.atlassian.net/jira/software/projects/IDS5/"
                                                 "boards/7?selectedIssue=IDS5-7"))));
        // Not an issue, another host, or without TLS
        QVERIFY(!jira.handles(QUrl(QStringLiteral("https://example.atlassian.net/wiki/spaces/X"))));
        QVERIFY(!jira.handles(QUrl(QStringLiteral("https://elsewhere.example/browse/IDS5-7"))));
        QVERIFY(!jira.handles(QUrl(QStringLiteral("http://example.atlassian.net/browse/IDS5-7"))));
        QVERIFY(links.info(QStringLiteral("https://elsewhere.example/browse/IDS5-7")).isEmpty());
    }

    void issueWithEmailAndToken()
    {
        FakeJira server("Basic " + QByteArray("ada@example.org:secret").toBase64());
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), server.host(), QStringLiteral("ada@example.org")},
                                   "secret");
        QSignalSpy revision(&links, &Links::revisionChanged);
        QVERIFY(links.info(server.issue("IDS5-7")).isEmpty()); // asked in the background
        QTRY_COMPARE(revision.size(), 1);
        const QVariantMap info = links.info(server.issue("IDS5-7"));
        QCOMPARE(info.value("title").toString(), QStringLiteral("IDS5-7"));
        QCOMPARE(info.value("summary").toString(), QStringLiteral("Fix the crash on resize"));
        QCOMPARE(info.value("status").toString(), QStringLiteral("In Progress"));
        QCOMPARE(info.value("tone").toString(), QStringLiteral("progress"));
        QStringList rows;
        for (const QVariant &row : info.value("fields").toList())
            rows << row.toMap().value("name").toString() + "=" + row.toMap().value("value").toString();
        QCOMPARE(rows.mid(0, 4), QStringList({"Type=Bug", "Priority=P1", "Assignee=Unassigned",
                                              "Reporter=Grace Hopper"}));
        QVERIFY(rows.last().startsWith("Updated="));
        // Fresh: not asked again
        QCOMPARE(server.requests.size(), 1);
        links.info(server.issue("IDS5-7"));
        QTest::qWait(50);
        QCOMPARE(server.requests.size(), 1);
    }

    void issueWithBearerToken()
    {
        FakeJira server("Bearer pat");
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), server.host(), {}}, "pat");
        links.info(server.issue("IDS5-7"));
        QTRY_COMPARE(links.info(server.issue("IDS5-7")).value("summary").toString(),
                     QStringLiteral("Fix the crash on resize"));
    }

    void missingIssueRestsAWhile()
    {
        FakeJira server("Bearer pat");
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), server.host(), {}}, "pat");
        links.info(server.issue("IDS5-404"));
        QTRY_COMPARE(server.requests.size(), 1);
        QTest::qWait(50);
        QVERIFY(links.info(server.issue("IDS5-404")).isEmpty());
        QTest::qWait(50);
        QCOMPARE(server.requests.size(), 1); // not asked again at once
    }

    void addAccountSignsInFirst()
    {
        FakeJira server("Bearer right");
        Links links{Links::Detached{}};
        QSignalSpy added(&links, &Links::accountAdded);

        links.addAccount(QStringLiteral("jira"), server.host(), {}, QStringLiteral("wrong"));
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(added.at(0).at(0).toBool(), false);
        QVERIFY(links.accounts()->list().isEmpty());

        links.addAccount(QStringLiteral("jira"), QStringLiteral("http://%1/browse/IDS5-7").arg(server.host()), {},
                         QStringLiteral(" right "));
        QTRY_COMPARE(added.size(), 2);
        QCOMPARE(added.at(1).at(0).toBool(), true);
        QVERIFY(added.at(1).at(1).toString().contains(QStringLiteral("Ada Lovelace")));
        QCOMPARE(links.accounts()->list().size(), 1);
        QCOMPARE(links.accounts()->list().first().toMap().value("host").toString(), server.host());

        links.accounts()->remove(QStringLiteral("jira"), server.host());
        QVERIFY(links.accounts()->list().isEmpty());
        QVERIFY(links.info(server.issue("IDS5-7")).isEmpty());
    }

    void tokenNeverFollowsARedirect()
    {
        FakeJira elsewhere("Bearer pat");
        FakeJira server("Bearer pat", elsewhere.issue("IDS5-7"));
        Links links{Links::Detached{}};
        QSignalSpy added(&links, &Links::accountAdded);
        links.addAccount(QStringLiteral("jira"), server.host(), {}, QStringLiteral("pat"));
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(added.at(0).at(0).toBool(), false);
        QTest::qWait(50);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(elsewhere.requests.isEmpty());
    }
};

QTEST_MAIN(TestLinks)
#include "tst_links.moc"
