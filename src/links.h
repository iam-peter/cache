#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QNetworkAccessManager;

// What a resolver found out about a link: shown after it in a note, and in
// the card over it
struct LinkInfo
{
    QString title;   // short: an issue's key
    QString summary; // a line about it: the issue's summary
    QString status;  // "In Progress"
    QString tone;    // the status as "todo", "progress" or "done"
    QList<QPair<QString, QString>> fields; // the card's rows, in order
    QString image;   // a picture of it, from the web (a page's og:image)
    QString site;    // where it is: "github.com", "Jira · example.atlassian.net"
    QString kind;    // "design" for a Figma frame or file: its picture is the point
    QString version;     // of what it shows, as its service counts (a Figma file's)
    QString remoteImage; // where `image` came from, when it is kept on disk
    int renderSize = 0;  // a render's longest side in pixels (Figma's); 0: not one
    int unresolved = 0;  // comments still to be resolved (a Gerrit change's)
    QString label;       // a short name, where the title is long: "600001"; empty: the title
    // Chips beside the status: [{name, short, value, tone}], such as a
    // review label's vote ("Code-Review", "CR", "+2", "done")
    QVariantList badges;
    QString code;    // lines of a file (GitLab), shown highlighted
    QString language;
    int firstLine = 1;

    QVariantMap toVariant() const;
    static LinkInfo fromVariant(const QVariantMap &map);
};

// An account on a host that a resolver reads from. Its token is in the
// system's keychain, never in the settings.
struct Account
{
    QString provider; // the resolver's id: "jira"
    // "example.atlassian.net", with ":port" if it has one; or kEverySite:
    // one Atlassian sign-in (an email and API token of id.atlassian.com)
    // for every Atlassian Cloud site
    QString host;
    QString user; // Jira Cloud: the email the token is of; empty for a bearer token
    // Other hosts whose links it answers for, such as the old address of a
    // Jira that moved: "old.example.com", read from `host`, or
    // "old.example.com=site.atlassian.net", read from that site (every
    // site's account must say which). Nothing goes to them.
    QStringList aliases;
    // Figma: the teams whose files the command palette searches (their ids)
    QStringList teams;
    // Figma: library files whose components, sets and frames it searches
    // too (their keys), published in a listed team or not
    QStringList libraries;

    static inline const QString kEverySite = QStringLiteral("*.atlassian.net");
    bool everySite() const { return host == kEverySite; }
    // The host to ask about a link on `linkHost`: the account's own, an
    // alias's site, or for every site the link's own; empty when the
    // account doesn't answer for it. The token goes nowhere else.
    QString siteFor(const QString &linkHost) const;
};

// The accounts, kept in the settings, and their tokens, read from the
// keychain once and then held for the session
class Accounts : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    // [{provider, host, user, aliases}]
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    // Whether tokens can be kept (Linux builds need libsecret)
    Q_PROPERTY(bool keychain READ keychain CONSTANT)

public:
    // `persistent`: read from and saved to the settings (not in tests)
    explicit Accounts(bool persistent, QObject *parent = nullptr);

    QVariantList list() const;
    bool keychain() const;

    // The account for `host`, by its own host or one of its aliases
    std::optional<Account> find(const QString &provider, const QString &host) const;
    // The other hosts the account answers for, from what people type:
    // addresses or host names, separated by commas or spaces
    Q_INVOKABLE void setAliases(const QString &provider, const QString &host, const QString &aliases);
    // The teams whose files are searched, from their ids or addresses
    // (figma.com/files/team/123/…)
    Q_INVOKABLE void setTeams(const QString &provider, const QString &host, const QString &teams);
    // The library files searched, from their addresses or keys
    Q_INVOKABLE void setLibraries(const QString &provider, const QString &host, const QString &libraries);
    const QList<Account> &all() const { return m_accounts; }
    // The account's token, or empty when there is none. From the keychain
    // the first time, in the background: unlocking it may ask the user.
    void token(const Account &account, const std::function<void(const QByteArray &)> &done);
    // The account, its token held for this session (tests; and once
    // signed in, so the keychain needn't be asked again)
    void remember(const Account &account, const QByteArray &token);
    // Kept: the account in the settings, its token in the keychain
    void save(const Account &account, const QByteArray &token,
              const std::function<void(bool ok, const QString &error)> &done);
    Q_INVOKABLE void remove(const QString &provider, const QString &host);

    // "jira/example.atlassian.net": the token's key in the keychain
    static QString keyOf(const Account &account);

signals:
    void changed();

private:
    void add(const Account &account);
    void store() const;

    const bool m_persistent;
    QList<Account> m_accounts;
    QHash<QString, QByteArray> m_tokens; // by keyOf
    QHash<QString, QList<std::function<void(const QByteArray &)>>> m_waiting;
};

// Reads what a link points to, for the links it knows: Jira issues, say.
// Each provider is one of these, given to Links in the order to ask them.
class LinkResolver
{
public:
    using Done = std::function<void(const std::optional<LinkInfo> &info, const QString &error)>;
    using SignedIn = std::function<void(bool ok, const QString &message)>;

    virtual ~LinkResolver() = default;
    virtual QString id() const = 0;   // "jira", as accounts name it
    virtual QString name() const = 0; // "Jira", as people do
    virtual bool handles(const QUrl &url) const = 0;
    virtual void resolve(const QUrl &url, const Done &done) = 0;
    // Tries an account before it is kept: `message` says who it signed in
    // as, or what went wrong
    virtual void signIn(const Account &account, const QByteArray &token, const SignedIn &done) = 0;
    // Whether accounts are made for it (Settings lists it); Confluence
    // reads with the Atlassian accounts Jira's sign-in makes
    virtual bool signsIn() const { return true; }
    // The one host its accounts are for, whatever people type (Figma's);
    // empty when they name the host
    virtual QString fixedHost() const { return {}; }
    // Whether its accounts name a user besides the token (Atlassian Cloud's
    // email)
    virtual bool needsUser() const { return false; }
    // What it finds for `query` with its accounts, for the command palette:
    // [{title, detail, url, status, tone, image}]. Called back once.
    using Results = std::function<void(const QVariantList &results)>;
    virtual void search(const QString &query, const Results &done) { Q_UNUSED(query) done({}); }
    // A sharper picture of what `url` shows, `pixels` on its longest side,
    // when the one there is is shown larger (a Figma render): false when
    // it can't make one
    virtual bool sharpen(const QUrl &url, int pixels, const Done &done)
    {
        Q_UNUSED(url) Q_UNUSED(pixels) Q_UNUSED(done)
        return false;
    }
    // What waits for the user's review there (merge and pull requests),
    // for the Tasks tab
    virtual void reviews(const Results &done) { done({}); }
    // What it keeps besides answers to links (lists to search): forgotten,
    // so it is read anew when next needed
    virtual void forget() {}
    // Whether it can tell what an issue (a Jira key) led to: Gerrit's
    // changes whose commit message names it
    virtual bool relates() const { return false; }
    // What issue `key` led to, as search results
    virtual void related(const QString &key, const Results &done)
    {
        Q_UNUSED(key)
        done({});
    }
};

// Links to what other services hold, read through their resolvers. What
// they found is kept in the cache folder (not in the project, which may be
// shared), shown at once and read again once it is a while old.
class Links : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Links)
    QML_SINGLETON

    // Goes up whenever something about a link came in: bindings that call
    // info() ask again
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    // Whether links are being read now
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(Accounts *accounts READ accounts CONSTANT)
    // [{id, name, fixedHost, needsUser}] of the resolvers that sign in
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)
    // GitLab merge requests and GitHub pull requests waiting for the
    // user's review, for the Tasks tab: [{title, detail, url, status,
    // tone}]; see refreshReviews()
    Q_PROPERTY(QVariantList reviews READ reviews NOTIFY reviewsChanged)

public:
    explicit Links(QObject *parent = nullptr);
    // Tests: nothing read from or kept in the settings or the cache folder
    struct Detached {};
    Links(Detached, QObject *parent = nullptr);
    ~Links() override;

    int revision() const { return m_revision; }
    Accounts *accounts() const { return m_accounts; }
    QVariantList providers() const;
    QNetworkAccessManager *network();

    // Asked before the resolvers there are
    void addResolver(std::unique_ptr<LinkResolver> resolver);
    // One that needs no account (Wikipedia): asked for cards and hover
    // cards, never for what a note shows, before any page is read as is
    void addOpenResolver(std::unique_ptr<LinkResolver> resolver);
    LinkResolver *resolver(const QString &id) const;

    // What is known about `url`: {title, summary, status, tone,
    // fields: [{name, value}], image, site}, or, when it couldn't be read
    // and nothing is known, {error}; or nothing. Asks its resolver
    // in the background when nothing is known yet, or only something old.
    // Only services with an account: notes ask this for every web link.
    Q_INVOKABLE QVariantMap info(const QString &url);
    // The same for a link card or a hover card: any web page, read as it
    // is. Asked only when someone means it (a card, a pointer resting).
    Q_INVOKABLE QVariantMap card(const QString &url);
    // The details link cards of service `source` (a card's "source": jira,
    // gerrit, page…) show unless they say otherwise (see BoardItem::fields);
    // empty: as usual
    Q_INVOKABLE QStringList cardFields(const QString &source) const;
    Q_INVOKABLE void setCardFields(const QString &source, const QStringList &fields);
    // Asks again now (a card's Reload)
    Q_INVOKABLE void reload(const QString &url);
    // Signs in, then keeps the account: accountAdded says how it went.
    // `everySite`: an Atlassian Cloud host stands for every such site.
    Q_INVOKABLE void addAccount(const QString &provider, const QString &host, const QString &user,
                                const QString &token, bool everySite = false);
    // Asks bindings that call info() again (once the board can ask)
    void touch();
    // The picture of `url` is shown `pixels` large on its longest side: a
    // sharper one, in steps (800, 1600, 3200, 4096), if the one there is is
    // smaller and its service can make one
    Q_INVOKABLE void sharpen(const QString &url, int pixels);

    // What `resolver` last found for `url`, if anything: to reuse what
    // hasn't changed (a Figma render while the file's version is the same)
    std::optional<LinkInfo> known(const QString &resolver, const QString &url) const;
    // Where pictures of links are kept, so they show at once next time;
    // empty: not kept (the default when detached, for tests)
    void setImageFolder(const QString &folder) { m_imageFolder = folder; }

    // The account for a link, and the site to ask: by the link's host, or
    // by the Atlassian site that host turned out to be an address of
    std::optional<std::pair<Account, QString>> accountFor(const QString &provider, const QUrl &url) const;
    // Searches every service with an account for `query` (three letters or
    // more); each answers with found(), its source being the resolver's id
    // Only those of `sources` (their ids: jira, figma…) when given
    Q_INVOKABLE void search(const QString &query, const QStringList &sources = {});
    QVariantList reviews() const { return m_reviews; }
    // Asks GitLab and GitHub again what waits for the user's review
    Q_INVOKABLE void refreshReviews();
    // Everything read anew: each link when next shown (what was read shows
    // till then), the services' search lists, what waits for review
    Q_INVOKABLE void refresh();
    bool busy() const { return m_reading > 0; }
    // Whether `url` is an issue whose changes a service with an account
    // can find (Gerrit's, by the issue's key)
    Q_INVOKABLE bool canRelate(const QString &url) const;
    // The issue key in `url`: "QTBUG-152002"; empty if none
    Q_INVOKABLE QString issueKey(const QString &url) const;
    // The changes issue `url` led to, from every service that can tell:
    // answered with relatedFound()
    Q_INVOKABLE void related(const QString &url);
    // The Atlassian sites to search: those with an account of their own,
    // and for an every-site account the sites links have been read from
    QStringList atlassianSites() const;

    // A link that may be read with an account: https, or http on loopback
    static bool secure(const QUrl &url);
    // GET `path` of `site` with the account's token, its answer as JSON.
    // Built from the site the account gives, never from a link; no
    // redirect is followed, so a sign-in page elsewhere gets nothing.
    using JsonReply = std::function<void(int status, const QJsonObject &json, const QString &error)>;
    void request(const QString &site, const Account &account, const QByteArray &token, const QString &path,
                 const JsonReply &reply);
    // The same, the answer as it came (a JSON array, a file's text)
    using BytesReply = std::function<void(int status, const QByteArray &body, const QString &error)>;
    void requestBytes(const QString &site, const Account &account, const QByteArray &token, const QString &path,
                      const BytesReply &reply);
    // Code as HTML for a card: escaped, coloured as notes colour it
    Q_INVOKABLE QString highlight(const QString &code, const QString &language, const QVariantMap &colors) const;

    // The Atlassian site `host` is another address of, found out by asking
    // it (see discover()); empty when not known (yet) or when it is none
    QString movedTo(const QString &host) const { return m_moved.value(host.toLower()); }

    // "example.atlassian.net" from what people paste: a whole address, or
    // the host with or without a scheme
    static QString hostOf(const QString &text);
    // The host and port of `url`, as accounts name it
    static QString authority(const QUrl &url);
    // Only loopback hosts (tests, a local proxy) may be spoken to without TLS
    static bool loopback(const QString &host);
    // An Atlassian Cloud site: "example.atlassian.net"
    static bool atlassianSite(const QString &host);

    static constexpr int kFreshSeconds = 15 * 60;
    static constexpr int kRetrySeconds = 5 * 60;

signals:
    void revisionChanged();
    void busyChanged();
    void accountAdded(bool ok, const QString &message);
    void found(const QString &query, const QString &source, const QVariantList &results);
    void reviewsChanged();
    // The changes an issue led to: rows as search() gives them
    void relatedFound(const QString &url, const QString &key, const QVariantList &results);

private:
    struct Entry
    {
        std::optional<LinkInfo> info;
        QDateTime fetched;
        QDateTime failed;
        QString error; // why it couldn't be read, last time
        bool pending = false;
    };

    LinkResolver *resolverFor(const QUrl &url) const;
    // Whether `host` is the custom domain of an Atlassian Cloud site: its
    // serverInfo names the site (asked without signing in, so nothing is
    // sent), and both give the same cloud id, so a host can't just claim
    // a site. Once per host; the answer is kept in the cache folder.
    void discover(const QString &host);
    QVariantMap lookUp(const QString &url, LinkResolver *resolver);
    void load();
    void save();
    // A picture from the web into the image folder; the entry shows it
    // once it is there
    void keepImage(const QString &key, const QString &remote);
    // An answer for the entry `key`: kept, its picture fetched to disk
    void settle(const QString &key, const std::optional<LinkInfo> &info, const QString &error);
    void bump();

    const bool m_persistent;
    Accounts *m_accounts;
    QNetworkAccessManager *m_network = nullptr;
    std::vector<std::unique_ptr<LinkResolver>> m_resolvers;
    std::vector<std::unique_ptr<LinkResolver>> m_open; // no account, for cards
    std::unique_ptr<LinkResolver> m_page; // for cards, when no other knows the link
    QHash<QString, Entry> m_entries; // by resolver and URL: "jira https://…"
    QHash<QString, QString> m_moved;  // host -> its Atlassian site ("" for none)
    QSet<QString> m_discovering;
    QVariantList m_reviews;
    QString m_imageFolder;
    QSet<QString> m_downloading; // pictures being fetched, by their address
    QSet<QString> m_sharpening;  // entries a sharper picture is asked for
    int m_reading = 0;           // entries being read
    QHash<QString, int> m_sharpened; // the largest step asked for, by entry
    bool m_loaded = false;
    QTimer m_bump; // answers come in batches: one revision for each
    QTimer m_save;
    int m_revision = 0;
};
