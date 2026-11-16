#include "confluenceresolver.h"
#include "figmaresolver.h"
#include "gitlabresolver.h"
#include "jiraresolver.h"
#include "links.h"
#include "pageresolver.h"
#include "wikipediaresolver.h"

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
                    if (noJira && request.startsWith("GET /rest/api/2/")
                        && !request.startsWith("GET /rest/api/2/serverInfo ")) {
                        status = "404 Not Found";
                        body = "{}";
                    } else if (request.startsWith("GET /_edge/tenant_info ")) {
                        body = R"({"cloudId":"tenant-1"})";
                    } else if (request.startsWith("GET /rest/api/2/serverInfo ")) {
                        body = R"({"deploymentType":"Cloud","baseUrl":"http://)" + host().toUtf8() + "\"}";
                    } else if (!redirectTo.isEmpty()) {
                        status = "302 Found";
                        extra = "Location: " + redirectTo.toUtf8() + "\r\n";
                    } else if (!request.contains("\r\nAuthorization: " + authorization + "\r\n")) {
                        status = "401 Unauthorized";
                        body = R"({"errorMessages":["You are not authenticated."]})";
                    } else if (request.startsWith("GET /rest/api/2/myself ")) {
                        body = R"({"displayName":"Ada Lovelace"})";
                    } else if (request.startsWith("GET /wiki/rest/api/user/current ")) {
                        body = R"({"publicName":"Ada L."})";
                    } else if (request.startsWith("GET /wiki/rest/api/content/1174503652?")
                               || request.startsWith("GET /rest/api/content?spaceKey=DTU&title=Variables%20-%20user%20scenarios&")) {
                        const QByteArray page = R"({"id":"1174503652","type":"page","title":"Variables - user scenarios",
                            "space":{"key":"DTU","name":"Design Tools"},
                            "version":{"when":"2026-11-10T09:30:00.000Z","by":{"displayName":"Grace Hopper"}},
                            "history":{"createdBy":{"displayName":"Ada Lovelace"}}})";
                        body = request.startsWith("GET /rest/api/content?") ? "{\"results\":[" + page + "]}" : page;
                    } else if (request.startsWith("GET /rest/api/2/search?jql=")) {
                        body = R"({"issues":[{"key":"IDS5-7","fields":{"summary":"Fix the crash on resize",
                            "status":{"name":"In Progress","statusCategory":{"key":"indeterminate"}}}}]})";
                    } else if (request.startsWith("GET /rest/api/search?limit=8&cql=text")) {
                        body = R"({"_links":{"base":"http://wiki.example"},"results":[{"title":"@@@hl@@@Gradient@@@endhl@@@ controls",
                            "url":"/spaces/DTU/pages/9/Gradient","excerpt":"How the controls work",
                            "resultGlobalContainer":{"title":"Design Tools"}}]})";
                    } else if (request.startsWith("GET /wiki/rest/api/search?") || request.startsWith("GET /rest/api/search?")) {
                        body = R"({"results":[{"excerpt":"How @@@hl@@@variables@@@endhl@@@ are used &amp; shared"}]})";
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
    bool noJira = false; // a host with only Confluence
    QString issue(const char *key) const
    {
        return QStringLiteral("http://%1/browse/%2").arg(host(), QLatin1String(key));
    }
    QList<QByteArray> requests;
};

// Another address of a Jira Cloud site: it names the site and its tenant
class CustomDomain : public QTcpServer
{
public:
    CustomDomain(const QString &site, const QByteArray &cloudId)
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this, site, cloudId] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket, site, cloudId] {
                    const QByteArray request = socket->readAll();
                    requests << request;
                    QByteArray body = "{}";
                    if (request.startsWith("GET /rest/api/2/serverInfo "))
                        body = R"({"deploymentType":"Cloud","baseUrl":"http://)" + site.toUtf8() + "\"}";
                    else if (request.startsWith("GET /_edge/tenant_info "))
                        body = R"({"cloudId":")" + cloudId + "\"}";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString issue(const char *key) const
    {
        return QStringLiteral("http://localhost:%1/browse/%2").arg(serverPort()).arg(QLatin1String(key));
    }
    QList<QByteArray> requests;
};

// A stand-in for Figma's API: one file with one frame
class FakeFigma : public QTcpServer
{
public:
    FakeFigma()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray request = socket->readAll();
                    requests << request;
                    QByteArray status = "200 OK", body;
                    if (!request.contains("\r\nX-Figma-Token: figd_test\r\n")) {
                        status = "403 Forbidden";
                        body = R"({"status":403,"err":"Invalid token"})";
                    } else if (request.startsWith("GET /v1/me ")) {
                        body = R"({"handle":"Ada"})";
                    } else if (request.startsWith("GET /v1/teams/1234567/projects ")) {
                        body = R"({"projects":[{"id":7,"name":"Web"}]})";
                    } else if (request.startsWith("GET /v1/projects/7/files ")) {
                        body = R"({"files":[{"key":"AbCdEf123456","name":"Design System","thumbnail_url":"https://example.org/t.png"},
                            {"key":"ZyXwVu987654","name":"Marketing site"}]})";
                    } else if (request.startsWith("GET /v1/files/AbCdEf123456/nodes?ids=12%3A34&depth=1 ")) {
                        body = R"({"name":"Design System","lastModified":"2026-11-15T08:00:00Z","editorType":"figma",
                            "thumbnailUrl":"https://example.org/thumb.png",
                            "nodes":{"12:34":{"document":{"id":"12:34","name":"Button / Primary","type":"COMPONENT",
                            "absoluteBoundingBox":{"x":0,"y":0,"width":320,"height":80}}}}})";
                    } else if (request.startsWith("GET /v1/files/AbCdEf123456/nodes?ids=99%3A1&depth=1 ")) {
                        body = R"({"name":"Design System","nodes":{"99:1":null}})";
                    } else if (request.startsWith("GET /v1/images/AbCdEf123456?ids=12%3A34&format=png&scale=2 ")) {
                        body = R"({"err":null,"images":{"12:34":"https://example.org/render.png"}})";
                    } else if (request.startsWith("GET /v1/files/AbCdEf123456?depth=1 ")) {
                        body = R"({"name":"Design System","lastModified":"2026-11-15T08:00:00Z","editorType":"figjam",
                            "thumbnailUrl":"https://example.org/thumb.png",
                            "document":{"children":[{"name":"Cover"},{"name":"Components"}]}})";
                    } else {
                        status = "404 Not Found";
                        body = R"({"status":404,"err":"Not found"})";
                    }
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }
    QList<QByteArray> requests;
};

// A stand-in for GitLab's API: one project, group/app, with one of each
class FakeGitLab : public QTcpServer
{
public:
    FakeGitLab()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray request = socket->readAll();
                    requests << request;
                    const QByteArray path = request.mid(4, request.indexOf(' ', 4) - 4);
                    const QByteArray p = "/api/v4/projects/group%2Fapp";
                    QByteArray status = "200 OK", body;
                    const auto is = [&](const QByteArray &x) { return path == x; };
                    if (!request.contains("\r\nAuthorization: Bearer glpat-test\r\n")) {
                        status = "401 Unauthorized";
                        body = R"({"message":"401 Unauthorized"})";
                    } else if (is("/api/v4/user")) {
                        body = R"({"name":"Ada Lovelace","username":"ada"})";
                    } else if (is(p + "/merge_requests/12")) {
                        body = R"({"iid":12,"title":"Fix the crash on resize","state":"opened","draft":false,
                            "source_branch":"fix-resize","target_branch":"main","author":{"name":"Ada Lovelace"},
                            "reviewers":[{"name":"Grace Hopper"}],"head_pipeline":{"status":"running"},
                            "changes_count":"3","labels":["bug"],"user_notes_count":2,"updated_at":"2026-11-15T10:00:00Z"})";
                    } else if (is(p + "/merge_requests/12/approvals")) {
                        body = R"({"approved_by":[{"user":{"name":"Grace Hopper"}}]})";
                    } else if (is(p + "/pipelines/77")) {
                        body = R"({"id":77,"status":"failed","ref":"main","sha":"abcdef123456","duration":125,
                            "user":{"name":"Ada Lovelace"},"created_at":"2026-11-15T10:00:00Z"})";
                    } else if (is(p + "/pipelines/77/jobs?per_page=100&include_retried=false")) {
                        body = R"([{"stage":"test","status":"failed"},{"stage":"test","status":"success"},
                            {"stage":"build","status":"success"}])";
                    } else if (is(p + "/repository/files/src%2Fmain.cpp/raw?ref=main")) {
                        QStringList lines;
                        for (int i = 1; i <= 50; ++i)
                            lines << QStringLiteral("int line%1 = %1;").arg(i);
                        body = lines.join('\n').toUtf8();
                    } else if (is(p + "/issues/5")) {
                        body = R"({"iid":5,"title":"Crash on resize","state":"closed","assignees":[{"name":"Ada"}],
                            "labels":["bug","p1"],"user_notes_count":0})";
                    } else if (is(p + "/repository/commits/abc1234?stats=true")) {
                        body = R"({"short_id":"abc1234","title":"Fix it","author_name":"Ada",
                            "committed_date":"2026-11-15T10:00:00Z","stats":{"additions":10,"deletions":3},
                            "last_pipeline":{"status":"success"}})";
                    } else if (is(p + "/repository/commits/abc1234/refs?type=branch&per_page=10")) {
                        body = R"([{"name":"main"},{"name":"release"}])";
                    } else if (is(p)) {
                        body = R"({"name_with_namespace":"Group / App","description":"The app","default_branch":"main",
                            "star_count":4,"avatar_url":"https://example.org/a.png"})";
                    } else if (is(p + "/pipelines?per_page=1&ref=main")) {
                        body = R"([{"status":"success"}])";
                    } else if (is(p + "/languages")) {
                        body = R"({"QML":19.5,"C++":80.5})";
                    } else if (is(p + "/releases?per_page=1")) {
                        body = R"([{"name":"v1.0","released_at":"2026-11-01T10:00:00Z"}])";
                    } else if (path.startsWith(p + "/milestones?iids")) {
                        body = R"([{"id":99,"title":"1.0","state":"active","due_date":"2026-12-01"}])";
                    } else if (is(p + "/milestones/99/issues?per_page=100")) {
                        body = R"([{"state":"closed"},{"state":"opened"},{"state":"closed"}])";
                    } else if (path.startsWith("/api/v4/merge_requests?scope=all&state=opened&per_page=20&reviewer_username=ada")) {
                        body = R"([{"title":"Review me","references":{"full":"group/app!12"},"state":"opened",
                            "web_url":"https://gitlab.example/group/app/-/merge_requests/12"}])";
                    } else if (path.startsWith("/api/v4/search?scope=projects")) {
                        body = R"([{"name_with_namespace":"Group / App","web_url":"https://gitlab.example/group/app"}])";
                    } else if (path.startsWith("/api/v4/search?")) {
                        body = "[]";
                    } else {
                        status = "404 Not Found";
                        body = R"({"message":"404 Not found"})";
                    }
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString host() const { return QStringLiteral("127.0.0.1:%1").arg(serverPort()); }
    QString url(const char *path) const { return QStringLiteral("http://%1/group/app%2").arg(host(), QLatin1String(path)); }
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
        // An issue there: what would read it
        // (once it is known not to be another address of a site)
        const QString elsewhere = QStringLiteral("https://elsewhere.example/browse/IDS5-7");
        QTRY_VERIFY(links.info(elsewhere).value("error").toString().contains(QStringLiteral("elsewhere.example")));
        QVERIFY(!links.info(elsewhere).contains("title"));
        QVERIFY(links.info(QStringLiteral("https://elsewhere.example/wiki/page")).isEmpty());
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
        QVERIFY(!links.info(server.issue("IDS5-7")).contains("title"));
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
        QVERIFY(!links.info(QStringLiteral("https://example.atlassian.net/browse/IDS5-7")).contains("title"));
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
        QVERIFY(!links.info(old).contains("title"));
        links.accounts()->setAliases(QStringLiteral("jira"), server.host(),
                                     QStringLiteral("https://localhost:%1/browse/X-1, ").arg(server.serverPort()));
        QCOMPARE(links.accounts()->list().first().toMap().value("aliases").toStringList(),
                 QStringList{QStringLiteral("localhost:%1").arg(server.serverPort())});
        links.info(old);
        QTRY_COMPARE(links.info(old).value("summary").toString(), QStringLiteral("Fix the crash on resize"));
        // Asked of the account's own host, under its own name
        bool asked = false;
        for (const QByteArray &request : std::as_const(server.requests)) {
            if (request.startsWith("GET /rest/api/2/issue/")) {
                QVERIFY(request.contains("Host: " + server.host().toUtf8()));
                asked = true;
            }
        }
        QVERIFY(asked);
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

    void customDomainsAreFoundOut()
    {
        FakeJira site("Bearer pat");
        CustomDomain domain(site.host(), "tenant-1");
        CustomDomain forged(site.host(), "someone-else");
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), site.host(), {}}, "pat");

        // Looked up, then read from the site it is an address of
        QVERIFY(links.info(domain.issue("IDS5-7")).isEmpty());
        QTRY_COMPARE(links.info(domain.issue("IDS5-7")).value("summary").toString(),
                     QStringLiteral("Fix the crash on resize"));
        QCOMPARE(links.movedTo(QStringLiteral("localhost:%1").arg(domain.serverPort())), site.host());
        // Nothing of the user's went to the other address
        for (const QByteArray &request : std::as_const(domain.requests))
            QVERIFY(!request.contains("Authorization"));
        // Asked once
        const qsizetype asked = domain.requests.size();
        links.info(domain.issue("IDS5-8"));
        QTest::qWait(50);
        QCOMPARE(domain.requests.size(), asked);

        // A host that names the site but isn't it: not believed
        links.info(forged.issue("IDS5-7"));
        QTRY_VERIFY(links.info(forged.issue("IDS5-7")).contains("error"));
        QVERIFY(links.movedTo(QStringLiteral("localhost:%1").arg(forged.serverPort())).isEmpty());
        QVERIFY(!links.info(forged.issue("IDS5-7")).contains("title"));
    }

    void confluenceLinks()
    {
        using C = ConfluenceResolver;
        const auto page = [](const char *url) { return C::pageOf(QUrl(QLatin1String(url))); };
        C::Page p = page("https://qtgroup.atlassian.net/wiki/spaces/dtu/pages/1174503652/Variables+-+user+scenarios");
        QCOMPARE(p.id, QStringLiteral("1174503652"));
        QCOMPARE(p.context, QStringLiteral("/wiki"));
        QCOMPARE(page("https://x.atlassian.net/wiki/spaces/A/pages/edit-v2/42").id, QStringLiteral("42"));
        QCOMPARE(page("https://x.atlassian.net/wiki/spaces/A/blog/2026/11/16/77/News").id, QStringLiteral("77"));
        QCOMPARE(page("https://wiki.example.com/confluence/pages/viewpage.action?pageId=99").id, QStringLiteral("99"));
        QCOMPARE(page("https://wiki.example.com/confluence/pages/viewpage.action?pageId=99").context,
                 QStringLiteral("/confluence"));
        p = page("https://wiki.example.com/display/DTU/Variables+-+user+scenarios");
        QCOMPARE(p.space, QStringLiteral("DTU"));
        QCOMPARE(p.title, QStringLiteral("Variables - user scenarios"));
        QCOMPARE(p.context, QString());
        // Short links stand for the id
        for (const QString &id : {QStringLiteral("1174503652"), QStringLiteral("42"), QStringLiteral("65536")}) {
            const QString tiny = C::tinyOf(id);
            QCOMPARE(C::idOfTiny(tiny), id);
            QCOMPARE(page(qPrintable(QStringLiteral("https://x.atlassian.net/wiki/x/") + tiny)).id, id);
        }
        // Not a page, and not just any site's /pages/1
        QVERIFY(!page("https://x.atlassian.net/wiki/spaces/A/overview").valid());
        QVERIFY(!page("https://x.atlassian.net/browse/IDS5-1").valid());
        QVERIFY(!C::looksLike(QUrl(QStringLiteral("https://blog.example.com/pages/123/hello"))));
        QVERIFY(C::looksLike(QUrl(QStringLiteral("https://wiki.example.com/wiki/spaces/A/pages/1/T"))));
    }

    void confluencePagesAreRead()
    {
        FakeJira server("Basic " + QByteArray("ada@example.org:secret").toBase64());
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), server.host(), QStringLiteral("ada@example.org")},
                                   "secret");
        // By id, as Atlassian Cloud links have it
        const QString cloud = QStringLiteral("http://%1/wiki/spaces/DTU/pages/1174503652/Variables+-+user+scenarios")
                                  .arg(server.host());
        links.info(cloud);
        QTRY_COMPARE(links.info(cloud).value("title").toString(), QStringLiteral("Variables - user scenarios"));
        const QVariantMap info = links.info(cloud);
        QCOMPARE(info.value("summary").toString(), QStringLiteral("How variables are used & shared"));
        QCOMPARE(info.value("site").toString(), QStringLiteral("Confluence · ") + server.host());
        QStringList rows;
        for (const QVariant &row : info.value("fields").toList())
            rows << row.toMap().value("name").toString() + "=" + row.toMap().value("value").toString();
        QCOMPARE(rows.first(), QStringLiteral("Space=Design Tools"));
        QVERIFY(rows.contains(QStringLiteral("Updated by=Grace Hopper")));
        QVERIFY(rows.contains(QStringLiteral("Created by=Ada Lovelace")));
        // By space and title, as Data Center's /display links have it
        const QString display = QStringLiteral("http://%1/display/DTU/Variables+-+user+scenarios").arg(server.host());
        links.card(display);
        QTRY_COMPARE(links.card(display).value("title").toString(), QStringLiteral("Variables - user scenarios"));
        // A missing page says so
        const QString missing = QStringLiteral("http://%1/wiki/spaces/DTU/pages/5/Gone").arg(server.host());
        links.info(missing);
        QTRY_VERIFY(links.info(missing).value("error").toString().startsWith(server.host()));
    }

    void signInOnAHostWithOnlyConfluence()
    {
        FakeJira server("Bearer pat");
        server.noJira = true;
        Links links{Links::Detached{}};
        QSignalSpy added(&links, &Links::accountAdded);
        links.addAccount(QStringLiteral("jira"), server.host(), {}, QStringLiteral("pat"));
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(added.first().at(0).toBool(), true);
        QVERIFY(added.first().at(1).toString().contains(QStringLiteral("Ada L.")));
        // Confluence isn't an account of its own
        for (const QVariant &p : links.providers())
            QVERIFY(p.toMap().value("id").toString() != QStringLiteral("confluence"));
    }

    void figmaLinks()
    {
        const auto ref = [](const char *url) { return FigmaResolver::refOf(QUrl(QLatin1String(url))); };
        FigmaResolver::Ref r = ref("https://www.figma.com/design/AbCdEf123456/Design-System?node-id=12-34&t=x");
        QCOMPARE(r.file, QStringLiteral("AbCdEf123456"));
        QCOMPARE(r.node, QStringLiteral("12:34"));
        QCOMPARE(ref("https://www.figma.com/file/AbCdEf123456/Old").node, QString());
        QCOMPARE(ref("https://www.figma.com/board/AbCdEf123456/Board").file, QStringLiteral("AbCdEf123456"));
        QCOMPARE(ref("https://www.figma.com/proto/AbCdEf123456/P?node-id=1-2&starting-point-node-id=1%3A2").node,
                 QStringLiteral("1:2"));
        QCOMPARE(ref("https://www.figma.com/design/AbCdEf123456/branch/BrAnCh654321/DS").file,
                 QStringLiteral("BrAnCh654321"));
        QCOMPARE(ref("https://www.figma.com/design/AbCdEf123456/DS?node-id=I1-2%3B3-4").node, QStringLiteral("I1:2;3:4"));
        QVERIFY(!ref("https://www.figma.com/files/recent").valid());
        QVERIFY(!ref("https://www.figma.com/community/file/123").valid());
        QVERIFY(!ref("https://figma.example.com/design/AbCdEf123456/X").valid());
    }

    void figmaDesignsAreRead()
    {
        FakeFigma api;
        Links links{Links::Detached{}};
        static_cast<FigmaResolver *>(links.resolver(QStringLiteral("figma")))->setApi(api.base());
        QSignalSpy added(&links, &Links::accountAdded);
        // Signing in: just a token, for figma.com
        links.addAccount(QStringLiteral("figma"), QStringLiteral("ignored"), QStringLiteral("ignored"),
                         QStringLiteral("figd_wrong"));
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(added.at(0).at(0).toBool(), false);
        QVERIFY(added.at(0).at(1).toString().contains(QStringLiteral("Invalid token")));
        links.addAccount(QStringLiteral("figma"), {}, {}, QStringLiteral("figd_test"));
        QTRY_COMPARE(added.size(), 2);
        QCOMPARE(added.at(1).at(0).toBool(), true);
        QCOMPARE(links.accounts()->list().first().toMap().value("host").toString(), QStringLiteral("figma.com"));

        // A frame: its name, its file, and its picture rendered by Figma
        const QString frame = QStringLiteral("https://www.figma.com/design/AbCdEf123456/Design-System?node-id=12-34");
        links.card(frame);
        QTRY_COMPARE(links.card(frame).value("image").toString(), QStringLiteral("https://example.org/render.png"));
        QVariantMap info = links.card(frame);
        QCOMPARE(info.value("title").toString(), QStringLiteral("Button / Primary"));
        QCOMPARE(info.value("summary").toString(), QStringLiteral("Component in Design System"));
        QCOMPARE(info.value("kind").toString(), QStringLiteral("design"));
        QVERIFY(info.value("fields").toList().at(1).toMap().value("value").toString() == QStringLiteral("320 × 80"));
        // Notes ask too: it has an account
        QCOMPARE(links.info(frame).value("title").toString(), QStringLiteral("Button / Primary"));

        // The whole file: its pages and thumbnail
        const QString file = QStringLiteral("https://www.figma.com/board/AbCdEf123456/Board");
        links.card(file);
        QTRY_COMPARE(links.card(file).value("title").toString(), QStringLiteral("Design System"));
        info = links.card(file);
        QCOMPARE(info.value("summary").toString(), QStringLiteral("Pages: Cover, Components"));
        QCOMPARE(info.value("site").toString(), QStringLiteral("FigJam"));
        QCOMPARE(info.value("image").toString(), QStringLiteral("https://example.org/thumb.png"));

        // A frame that isn't there
        const QString gone = QStringLiteral("https://www.figma.com/design/AbCdEf123456/DS?node-id=99-1");
        links.card(gone);
        QTRY_VERIFY(links.card(gone).value("error").toString().contains(QStringLiteral("no such frame")));

        // The token went to the API only, as Figma wants it
        for (const QByteArray &request : std::as_const(api.requests))
            QVERIFY(!request.contains("Authorization"));
    }

    void wikipediaArticles()
    {
        using W = WikipediaResolver;
        W::Article a = W::articleOf(QUrl(QStringLiteral("https://de.wikipedia.org/wiki/Milz")));
        QCOMPARE(a.host, QStringLiteral("de.wikipedia.org"));
        QCOMPARE(a.title, QStringLiteral("Milz"));
        // The mobile site, a section, an encoded title
        a = W::articleOf(QUrl(QStringLiteral("https://en.m.wikipedia.org/wiki/Caf%C3%A9_au_lait#History")));
        QCOMPARE(a.host, QStringLiteral("en.wikipedia.org"));
        QCOMPARE(a.title, QStringLiteral("Caf%C3%A9_au_lait"));
        QVERIFY(!W::articleOf(QUrl(QStringLiteral("https://de.wikipedia.org/w/index.php?title=Milz"))).valid());
        QVERIFY(!W::articleOf(QUrl(QStringLiteral("https://de.wikipedia.org.example.com/wiki/Milz"))).valid());
        QVERIFY(!W::articleOf(QUrl(QStringLiteral("https://wikipedia.org/wiki/Milz"))).valid());

        // (Plain strings: a raw string spanning lines here threw moc off)
        PageServer api("application/json",
                       "{\"type\":\"standard\",\"title\":\"Milz\","
                       "\"description\":\"Organ des lymphatischen Systems\","
                       "\"extract\":\"Die Milz ist ein in den Blutkreislauf eingeschaltetes Organ.\","
                       "\"thumbnail\":{\"source\":\"https://upload.example.org/milz.png\",\"width\":330}}");
        Links links{Links::Detached{}};
        static_cast<W *>(links.resolver(QStringLiteral("wikipedia")))->setApi(api.url(""));
        const QString url = QStringLiteral("https://de.wikipedia.org/wiki/Milz");
        // Notes don't read it; cards and hover cards do
        QVERIFY(links.info(url).isEmpty());
        links.card(url);
        QTRY_COMPARE(links.card(url).value("title").toString(), QStringLiteral("Milz"));
        const QVariantMap info = links.card(url);
        QCOMPARE(info.value("summary").toString(), QStringLiteral("Die Milz ist ein in den Blutkreislauf eingeschaltetes Organ."));
        QCOMPARE(info.value("image").toString(), QStringLiteral("https://upload.example.org/milz.png"));
        QCOMPARE(info.value("site").toString(), QStringLiteral("Wikipedia · de"));
        QCOMPARE(info.value("fields").toList().first().toMap().value("value").toString(),
                 QStringLiteral("Organ des lymphatischen Systems"));
        QCOMPARE(api.requests, 1);
        QVERIFY(links.info(url).isEmpty());
    }

    void searchingServices()
    {
        FakeJira server("Bearer pat");
        FakeFigma figma;
        Links links{Links::Detached{}};
        links.accounts()->remember({QStringLiteral("jira"), server.host(), {}}, "pat");
        static_cast<FigmaResolver *>(links.resolver(QStringLiteral("figma")))->setApi(figma.base());
        links.accounts()->remember({QStringLiteral("figma"), QStringLiteral("figma.com"), {}}, "figd_test");
        links.accounts()->setTeams(QStringLiteral("figma"), QStringLiteral("figma.com"),
                                   QStringLiteral("https://www.figma.com/files/team/1234567/Team-Name"));
        QCOMPARE(links.accounts()->find(QStringLiteral("figma"), QStringLiteral("figma.com"))->teams,
                 QStringList{QStringLiteral("1234567")});
        QCOMPARE(links.atlassianSites(), QStringList{server.host()});

        QHash<QString, QVariantList> found;
        connect(&links, &Links::found, this, [&](const QString &query, const QString &source, const QVariantList &r) {
            QCOMPARE(query, QStringLiteral("gradient"));
            found.insert(source, r);
        });
        links.search(QStringLiteral("gr")); // too short: not asked
        links.search(QStringLiteral("gradient"));
        QTRY_COMPARE(found.size(), 4); // GitLab answers too, with nothing
        const QVariantMap issue = found.value("jira").first().toMap();
        QCOMPARE(issue.value("title").toString(), QStringLiteral("Fix the crash on resize"));
        QCOMPARE(issue.value("url").toString(), QStringLiteral("https://%1/browse/IDS5-7").arg(server.host()));
        QCOMPARE(issue.value("status").toString(), QStringLiteral("In Progress"));
        const QVariantMap page = found.value("confluence").first().toMap();
        QCOMPARE(page.value("title").toString(), QStringLiteral("Gradient controls"));
        QCOMPARE(page.value("url").toString(), QStringLiteral("http://wiki.example/spaces/DTU/pages/9/Gradient"));
        QVERIFY(page.value("detail").toString().startsWith(QStringLiteral("Design Tools")));
        // Figma by name: no file is called that
        QVERIFY(found.value("figma").isEmpty());

        found.clear();
        disconnect(&links, &Links::found, this, nullptr);
        connect(&links, &Links::found, this, [&](const QString &, const QString &source, const QVariantList &r) {
            found.insert(source, r);
        });
        links.search(QStringLiteral("design sys"));
        QTRY_COMPARE(found.size(), 4); // GitLab answers too, with nothing
        QCOMPARE(found.value("figma").size(), 1);
        QCOMPARE(found.value("figma").first().toMap().value("url").toString(),
                 QStringLiteral("https://www.figma.com/design/AbCdEf123456/Design-System"));
        // An issue key finds the issue itself
        found.clear();
        links.search(QStringLiteral("ids5-7"));
        QTRY_VERIFY(found.contains("jira"));
        QCOMPARE(found.value("jira").size(), 1); // once, though asked twice
    }

    void gitlabLinks()
    {
        using G = GitLabResolver;
        const auto ref = [](const char *url) { return G::refOf(QUrl(QLatin1String(url))); };
        G::Ref r = ref("https://gitlab.com/group/sub/app/-/merge_requests/12/diffs");
        QCOMPARE(r.project, QStringLiteral("group/sub/app"));
        QCOMPARE(r.kind, QStringLiteral("mr"));
        QCOMPARE(r.id, QStringLiteral("12"));
        r = ref("https://gitlab.com/group/app/-/blob/main/src/main.cpp#L40-60");
        QCOMPARE(r.kind, QStringLiteral("file"));
        QCOMPARE(r.ref, QStringLiteral("main"));
        QCOMPARE(r.path, QStringLiteral("src/main.cpp"));
        QCOMPARE(r.from, 40);
        QCOMPARE(r.to, 60);
        QCOMPARE(ref("https://gitlab.com/group/app/-/blob/main/x.py#L7").to, 7);
        QCOMPARE(ref("https://gitlab.com/group/app/-/pipelines/77").kind, QStringLiteral("pipeline"));
        QCOMPARE(ref("https://gitlab.com/group/app/-/commit/abc1234").kind, QStringLiteral("commit"));
        QCOMPARE(ref("https://gitlab.com/group/app/-/compare/v1...main").id, QStringLiteral("v1...main"));
        QCOMPARE(ref("https://gitlab.com/group/app/-/releases/v1.0").kind, QStringLiteral("release"));
        QCOMPARE(ref("https://gitlab.com/group/app/-/work_items/5").kind, QStringLiteral("issue"));
        QCOMPARE(ref("https://gitlab.com/group/app").kind, QStringLiteral("project"));
        QCOMPARE(ref("https://gitlab.com/group/app/-/tree/main").kind, QStringLiteral("project"));
        QVERIFY(!ref("https://gitlab.com/dashboard/merge_requests").valid());
        QVERIFY(!ref("https://gitlab.com/group").valid());
    }

    void gitlabWithoutAnAccount()
    {
        Links links{Links::Detached{}};
        // Not fetched: the sign-in page would say nothing
        const QVariantMap card = links.card(QStringLiteral("https://gitlab.example/group/app/-/merge_requests/12"));
        QCOMPARE(card.value("title").toString(), QStringLiteral("!12"));
        QVERIFY(card.value("summary").toString().contains(QStringLiteral("gitlab.example")));
    }

    void gitlabIsRead()
    {
        FakeGitLab server;
        Links links{Links::Detached{}};
        QSignalSpy added(&links, &Links::accountAdded);
        links.addAccount(QStringLiteral("gitlab"), server.host(), QStringLiteral("ignored"), QStringLiteral("glpat-wrong"));
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(added.at(0).at(0).toBool(), false);
        links.addAccount(QStringLiteral("gitlab"), server.host(), {}, QStringLiteral("glpat-test"));
        QTRY_COMPARE(added.size(), 2);
        QVERIFY(added.at(1).at(1).toString().contains(QStringLiteral("Ada Lovelace")));

        const auto read = [&](const QString &url) -> QVariantMap {
            links.card(url);
            QTest::qWaitFor([&] { return !links.card(url).isEmpty(); }, 5000);
            return links.card(url);
        };
        const auto field = [](const QVariantMap &info, const QString &name) {
            for (const QVariant &f : info.value("fields").toList()) {
                if (f.toMap().value("name").toString() == name)
                    return f.toMap().value("value").toString();
            }
            return QString();
        };
        QVariantMap info = read(server.url("/-/merge_requests/12"));
        QCOMPARE(info.value("title").toString(), QStringLiteral("!12"));
        QCOMPARE(info.value("summary").toString(), QStringLiteral("Fix the crash on resize"));
        QCOMPARE(info.value("status").toString(), QStringLiteral("Open"));
        QCOMPARE(field(info, "Branches"), QStringLiteral("fix-resize → main"));
        QCOMPARE(field(info, "Approved by"), QStringLiteral("Grace Hopper"));
        QCOMPARE(field(info, "Pipeline"), QStringLiteral("Running"));
        // In notes too, with an account
        QCOMPARE(links.info(server.url("/-/merge_requests/12")).value("title").toString(), QStringLiteral("!12"));

        info = read(server.url("/-/pipelines/77"));
        QCOMPARE(info.value("status").toString(), QStringLiteral("Failed"));
        QCOMPARE(info.value("tone").toString(), QStringLiteral("failed"));
        // Stages in order, each with how its jobs went
        QCOMPARE(info.value("fields").toList().at(0).toMap().value("name").toString(), QStringLiteral("Build"));
        QCOMPARE(field(info, "Test"), QStringLiteral("1 failed, 1 passed"));
        QCOMPARE(field(info, "Duration"), QStringLiteral("2:05"));

        info = read(server.url("/-/blob/main/src/main.cpp#L40-42"));
        QCOMPARE(info.value("code").toString(), QStringLiteral("int line40 = 40;\nint line41 = 41;\nint line42 = 42;"));
        QCOMPARE(info.value("firstLine").toInt(), 40);
        QCOMPARE(info.value("language").toString(), QStringLiteral("cpp"));
        QCOMPARE(info.value("title").toString(), QStringLiteral("main.cpp"));
        // Without lines: the start
        info = read(server.url("/-/blob/main/src/main.cpp"));
        QCOMPARE(info.value("code").toString().count('\n'), 29);

        info = read(server.url("/-/issues/5"));
        QCOMPARE(info.value("title").toString(), QStringLiteral("#5"));
        QCOMPARE(info.value("status").toString(), QStringLiteral("Closed"));
        QCOMPARE(field(info, "Labels"), QStringLiteral("bug, p1"));

        info = read(server.url("/-/commit/abc1234"));
        QCOMPARE(field(info, "Changes"), QStringLiteral("+10 −3"));
        QCOMPARE(field(info, "Branches"), QStringLiteral("main, release"));
        QCOMPARE(info.value("status").toString(), QStringLiteral("Passed"));

        info = read(server.url(""));
        QCOMPARE(info.value("title").toString(), QStringLiteral("Group / App"));
        QCOMPARE(field(info, "Languages"), QStringLiteral("C++ 81%, QML 20%"));
        QVERIFY(field(info, "Latest release").startsWith(QStringLiteral("v1.0")));
        QCOMPARE(info.value("status").toString(), QStringLiteral("Passed"));

        info = read(server.url("/-/milestones/3"));
        QCOMPARE(field(info, "Progress"), QStringLiteral("2 of 3 issues closed"));

        info = read(server.url("/-/merge_requests/999"));
        QVERIFY(info.value("error").toString().contains(QStringLiteral("Not found")));

        // The palette: search, and what waits for review
        QHash<QString, QVariantList> found;
        connect(&links, &Links::found, this, [&](const QString &, const QString &source, const QVariantList &r) {
            found.insert(source, r);
        });
        links.search(QStringLiteral("app"));
        QTRY_VERIFY(found.contains("gitlab"));
        QCOMPARE(found.value("gitlab").first().toMap().value("title").toString(), QStringLiteral("Group / App"));
        QSignalSpy reviews(&links, &Links::reviewsChanged);
        links.refreshReviews();
        QTRY_COMPARE(reviews.size(), 1);
        QCOMPARE(links.reviews().first().toMap().value("detail").toString(), QStringLiteral("group/app!12"));

        // The token went only as a bearer token, to this host
        for (const QByteArray &request : std::as_const(server.requests))
            QVERIFY(request.contains("Host: " + server.host().toUtf8()));
    }
};

QTEST_MAIN(TestLinks)
#include "tst_links.moc"
