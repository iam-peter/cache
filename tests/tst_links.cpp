#include "jiraresolver.h"
#include "links.h"
#include "pageresolver.h"

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

// Serves one page as `type`
class PageServer : public QTcpServer
{
public:
    PageServer(const QByteArray &type, const QByteArray &body)
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this, type, body] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, type, body] {
                    socket->readAll();
                    ++requests;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString url(const char *path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(QLatin1String(path)); }
    int requests = 0;
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
        QTRY_VERIFY(!links.info(server.issue("IDS5-7")).isEmpty());
        QVERIFY(revision.size() >= 1); // so bindings ask again
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
        // Why, and where it was asked
        QTRY_VERIFY(!links.info(server.issue("IDS5-404")).value("error").toString().isEmpty());
        const QString error = links.info(server.issue("IDS5-404")).value("error").toString();
        QVERIFY2(error.startsWith(server.host() + QStringLiteral(": Issue does not exist")), qPrintable(error));
        QVERIFY(!links.info(server.issue("IDS5-404")).contains("title"));
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

    void pagesSayWhatTheyAre()
    {
        const QUrl url(QStringLiteral("https://www.example.org/posts/1"));
        LinkInfo info = PageResolver::infoOf(url, QStringLiteral(
            "<html><head><title>Fallback</title>"
            "<meta content='A &amp; B' property='og:title'>"
            "<meta name=\"description\" content=\"Plain description\">"
            "<meta property=\"og:description\" content=\"Open Graph description\">"
            "<meta property=\"og:image\" content=\"/img/cover.png\">"
            "</head></html>"));
        QCOMPARE(info.title, QStringLiteral("A & B"));
        QCOMPARE(info.summary, QStringLiteral("Open Graph description"));
        QCOMPARE(info.image, QStringLiteral("https://www.example.org/img/cover.png"));
        QCOMPARE(info.site, QStringLiteral("example.org"));

        info = PageResolver::infoOf(url, QStringLiteral("<title>\n  Just a title\n</title>"));
        QCOMPARE(info.title, QStringLiteral("Just a title"));
        QVERIFY(info.summary.isEmpty());
        QVERIFY(info.image.isEmpty());
    }

    void cardsOfIssuesWithoutAnAccount()
    {
        Links links{Links::Detached{}};
        // Not fetched: the sign-in page would say nothing
        const QVariantMap card = links.card(QStringLiteral("https://example.atlassian.net/browse/IDS5-7"));
        QCOMPARE(card.value("title").toString(), QStringLiteral("IDS5-7"));
        QVERIFY(card.value("summary").toString().contains(QStringLiteral("example.atlassian.net")));
        // Notes only ask services with an account
        QVERIFY(links.info(QStringLiteral("https://example.atlassian.net/browse/IDS5-7")).isEmpty());
    }

    void cardsOfIssuesWithAnAccount()
    {
        FakeJira server("Bearer pat");
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), server.host(), {}}, "pat");
        links.card(server.issue("IDS5-7"));
        QTRY_COMPARE(links.card(server.issue("IDS5-7")).value("status").toString(), QStringLiteral("In Progress"));
        QCOMPARE(links.card(server.issue("IDS5-7")).value("site").toString(), QStringLiteral("Jira · ") + server.host());
        // Shared with what notes show
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(links.info(server.issue("IDS5-7")).value("summary").toString(),
                 QStringLiteral("Fix the crash on resize"));
        QCOMPARE(server.requests.size(), 1);
    }

    void cardsOfPages()
    {
        PageServer html("text/html; charset=utf-8",
                        "<head><meta property=\"og:title\" content=\"A page\"></head>");
        PageServer file("application/octet-stream", "data");
        Links links{Links::Detached{}};
        QSignalSpy revision(&links, &Links::revisionChanged);
        QVERIFY(links.card(html.url("/a")).isEmpty());
        QTRY_COMPARE(links.card(html.url("/a")).value("title").toString(), QStringLiteral("A page"));
        links.card(file.url("/report.bin"));
        QTRY_COMPARE(links.card(file.url("/report.bin")).value("title").toString(), QStringLiteral("report.bin"));
        QVERIFY(revision.size() >= 2);
    }

    void aliasesAreReadThroughTheirAccount()
    {
        FakeJira server("Bearer pat");
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), server.host(), {}}, "pat");
        // The old address: same port, another name for this machine
        const QString old = QStringLiteral("http://localhost:%1/browse/IDS5-7").arg(server.serverPort());
        QVERIFY(links.info(old).isEmpty());
        links.accounts()->setAliases(QStringLiteral("jira"), server.host(),
                                     QStringLiteral("https://localhost:%1/browse/X-1, ").arg(server.serverPort()));
        QCOMPARE(links.accounts()->list().first().toMap().value("aliases").toStringList(),
                 QStringList{QStringLiteral("localhost:%1").arg(server.serverPort())});
        links.info(old);
        QTRY_COMPARE(links.info(old).value("summary").toString(), QStringLiteral("Fix the crash on resize"));
        // Asked of the account's own host, under its own name
        QVERIFY(server.requests.last().contains("Host: " + server.host().toUtf8()));
        // Signing in again keeps them
        links.accounts()->remember({QStringLiteral("jira"), server.host(), {}}, "pat");
        QCOMPARE(links.accounts()->list().first().toMap().value("aliases").toStringList().size(), 1);
    }

    void oneSignInForEveryAtlassianSite()
    {
        Links links{Links::Detached{}};
        Accounts *accounts = links.accounts();
        const QString every = Account::kEverySite;
        accounts->remember({QStringLiteral("jira"), every, QStringLiteral("ada@example.org")}, "t");
        const auto site = [&](const QString &host) {
            const std::optional<Account> a = accounts->find(QStringLiteral("jira"), host);
            return a ? a->siteFor(host) : QStringLiteral("-");
        };
        QCOMPARE(site(QStringLiteral("qtgroup.atlassian.net")), QStringLiteral("qtgroup.atlassian.net"));
        QCOMPARE(site(QStringLiteral("qt-project.atlassian.net")), QStringLiteral("qt-project.atlassian.net"));
        // Nothing that only looks like one
        QCOMPARE(site(QStringLiteral("qtgroup.atlassian.net.example.com")), QStringLiteral("-"));
        QCOMPARE(site(QStringLiteral("evilatlassian.net")), QStringLiteral("-"));
        QCOMPARE(site(QStringLiteral("atlassian.net")), QStringLiteral("-"));
        JiraResolver jira(&links);
        QVERIFY(jira.handles(QUrl(QStringLiteral("https://qtgroup.atlassian.net/browse/IDS5-1341"))));

        // An old address names the site it moved to; one without a site,
        // or with one elsewhere, is left out
        accounts->setAliases(QStringLiteral("jira"), every,
                             QStringLiteral("https://jira.old.example.com/browse/X-1 = qt-project.atlassian.net, "
                                            "other.example.com; bad.example.com -> evil.example.com"));
        QCOMPARE(accounts->list().first().toMap().value("aliases").toStringList(),
                 QStringList{QStringLiteral("jira.old.example.com=qt-project.atlassian.net")});
        QCOMPARE(site(QStringLiteral("jira.old.example.com")), QStringLiteral("qt-project.atlassian.net"));
        QCOMPARE(site(QStringLiteral("bad.example.com")), QStringLiteral("-"));
        QVERIFY(jira.handles(QUrl(QStringLiteral("https://jira.old.example.com/browse/IDS5-1186"))));

        // A site's own account still comes first
        accounts->remember({QStringLiteral("jira"), QStringLiteral("qtgroup.atlassian.net"), {}}, "pat");
        QCOMPARE(accounts->find(QStringLiteral("jira"), QStringLiteral("qtgroup.atlassian.net"))->host,
                 QStringLiteral("qtgroup.atlassian.net"));
        QCOMPARE(accounts->find(QStringLiteral("jira"), QStringLiteral("other.atlassian.net"))->host, every);
    }

    void aliasesOfOneHostStayWithIt()
    {
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), QStringLiteral("jira.example.com"), {}}, "pat");
        // Pointing elsewhere would send the token there: left out
        links.accounts()->setAliases(QStringLiteral("jira"), QStringLiteral("jira.example.com"),
                                     QStringLiteral("old.example.com, older.example.com = evil.example.com, "
                                                    "oldest.example.com = jira.example.com"));
        QCOMPARE(links.accounts()->list().first().toMap().value("aliases").toStringList(),
                 QStringList({QStringLiteral("old.example.com"), QStringLiteral("oldest.example.com")}));
    }
};

QTEST_MAIN(TestLinks)
#include "tst_links.moc"
