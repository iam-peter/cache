#include "links.h"

#include "confluenceresolver.h"
#include "figmaresolver.h"
#include "githubresolver.h"
#include "gerritresolver.h"
#include "gitlabresolver.h"
#include "jiraresolver.h"
#include "highlight.h"
#include "keychain.h"
#include "markdown.h"
#include "pageresolver.h"
#include "wikipediaresolver.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>

// ---------------------------------------------------------------------------
// LinkInfo

QVariantMap LinkInfo::toVariant() const
{
    QVariantList rows;
    for (const auto &[name, value] : fields)
        rows << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("value"), value}};
    return {{QStringLiteral("title"), title},
            {QStringLiteral("summary"), summary},
            {QStringLiteral("status"), status},
            {QStringLiteral("tone"), tone},
            {QStringLiteral("fields"), rows},
            {QStringLiteral("image"), image},
            {QStringLiteral("site"), site},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("version"), version},
            {QStringLiteral("remoteImage"), remoteImage},
            {QStringLiteral("renderSize"), renderSize},
            {QStringLiteral("unresolved"), unresolved},
            {QStringLiteral("label"), label},
            {QStringLiteral("badges"), badges},
            {QStringLiteral("code"), code},
            {QStringLiteral("language"), language},
            {QStringLiteral("firstLine"), firstLine}};
}

LinkInfo LinkInfo::fromVariant(const QVariantMap &map)
{
    LinkInfo info;
    info.title = map.value(QStringLiteral("title")).toString();
    info.summary = map.value(QStringLiteral("summary")).toString();
    info.status = map.value(QStringLiteral("status")).toString();
    info.tone = map.value(QStringLiteral("tone")).toString();
    info.image = map.value(QStringLiteral("image")).toString();
    info.site = map.value(QStringLiteral("site")).toString();
    info.kind = map.value(QStringLiteral("kind")).toString();
    info.version = map.value(QStringLiteral("version")).toString();
    info.remoteImage = map.value(QStringLiteral("remoteImage")).toString();
    info.renderSize = map.value(QStringLiteral("renderSize")).toInt();
    info.unresolved = map.value(QStringLiteral("unresolved")).toInt();
    info.label = map.value(QStringLiteral("label")).toString();
    info.badges = map.value(QStringLiteral("badges")).toList();
    info.code = map.value(QStringLiteral("code")).toString();
    info.language = map.value(QStringLiteral("language")).toString();
    info.firstLine = map.value(QStringLiteral("firstLine"), 1).toInt();
    for (const QVariant &row : map.value(QStringLiteral("fields")).toList()) {
        const QVariantMap r = row.toMap();
        info.fields.append({r.value(QStringLiteral("name")).toString(), r.value(QStringLiteral("value")).toString()});
    }
    return info;
}

// ---------------------------------------------------------------------------
// Accounts

QString Account::siteFor(const QString &linkHost) const
{
    const QString h = linkHost.toLower();
    for (const QString &alias : aliases) {
        if (alias.section(QLatin1Char('='), 0, 0) == h) {
            const QString site = alias.section(QLatin1Char('='), 1);
            return !site.isEmpty() ? site : everySite() ? QString() : host;
        }
    }
    if (everySite())
        return Links::atlassianSite(h) ? h : QString();
    return host.compare(h, Qt::CaseInsensitive) == 0 ? host : QString();
}

Accounts::Accounts(bool persistent, QObject *parent)
    : QObject(parent)
    , m_persistent(persistent)
{
    if (!m_persistent)
        return;
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("accounts"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        Account account{settings.value(QStringLiteral("provider")).toString(),
                        settings.value(QStringLiteral("host")).toString(),
                        settings.value(QStringLiteral("user")).toString(),
                        settings.value(QStringLiteral("aliases")).toStringList(),
                        settings.value(QStringLiteral("teams")).toStringList()};
        account.libraries = settings.value(QStringLiteral("libraries")).toStringList();
        if (!account.provider.isEmpty() && !account.host.isEmpty())
            m_accounts.append(account);
    }
    settings.endArray();
}

QVariantList Accounts::list() const
{
    QVariantList list;
    for (const Account &a : m_accounts)
        list << QVariantMap{{QStringLiteral("provider"), a.provider},
                            {QStringLiteral("host"), a.host},
                            {QStringLiteral("user"), a.user},
                            {QStringLiteral("aliases"), a.aliases},
                            {QStringLiteral("teams"), a.teams},
                            {QStringLiteral("libraries"), a.libraries}};
    return list;
}

bool Accounts::keychain() const
{
    return Keychain::available();
}

std::optional<Account> Accounts::find(const QString &provider, const QString &host) const
{
    // The site's own account first, then one naming it as an old address,
    // then one for every site
    for (const Account &a : m_accounts) {
        if (a.provider == provider && !a.everySite() && a.host.compare(host, Qt::CaseInsensitive) == 0)
            return a;
    }
    for (const Account &a : m_accounts) {
        for (const QString &alias : a.aliases) {
            if (a.provider == provider && alias.section(QLatin1Char('='), 0, 0).compare(host, Qt::CaseInsensitive) == 0
                && !a.siteFor(host).isEmpty())
                return a;
        }
    }
    for (const Account &a : m_accounts) {
        if (a.provider == provider && a.everySite() && !a.siteFor(host).isEmpty())
            return a;
    }
    return std::nullopt;
}

void Accounts::setTeams(const QString &provider, const QString &host, const QString &teams)
{
    // The team's address, figma.com/files/<organisation>/team/<team>/…: the
    // number after team/ (the others are the organisation's and the user's).
    // Or ids alone.
    static const QRegularExpression inAddress(QStringLiteral(R"(team/(\d{6,}))"));
    static const QRegularExpression alone(QStringLiteral(R"((?:^|[\s,;])(\d{6,})(?=$|[\s,;]))"));
    QStringList ids;
    for (const QString &entry : teams.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch inside = inAddress.match(entry);
        const QRegularExpressionMatch bare = alone.match(entry);
        const QString team = inside.hasMatch() ? inside.captured(1) : bare.hasMatch() ? bare.captured(1) : QString();
        if (!team.isEmpty() && !ids.contains(team))
            ids << team;
    }
    for (Account &a : m_accounts) {
        if (a.provider != provider || a.host.compare(host, Qt::CaseInsensitive) != 0 || a.teams == ids)
            continue;
        a.teams = ids;
        if (m_persistent)
            store();
        emit changed();
    }
}

void Accounts::setLibraries(const QString &provider, const QString &host, const QString &libraries)
{
    // A file's address, figma.com/design/<key>/… (or file/, board/…), or
    // its key alone
    static const QRegularExpression inAddress(QStringLiteral(R"((?:design|file|board|proto|slides)/([A-Za-z0-9]{10,}))"));
    static const QRegularExpression alone(QStringLiteral(R"(^[A-Za-z0-9]{10,}$)"));
    QStringList keys;
    for (const QString &entry : libraries.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch inside = inAddress.match(entry);
        const QString key = inside.hasMatch() ? inside.captured(1) : alone.match(entry).hasMatch() ? entry : QString();
        if (!key.isEmpty() && !keys.contains(key))
            keys << key;
    }
    for (Account &a : m_accounts) {
        if (a.provider != provider || a.host.compare(host, Qt::CaseInsensitive) != 0 || a.libraries == keys)
            continue;
        a.libraries = keys;
        if (m_persistent)
            store();
        emit changed();
    }
}

void Accounts::setAliases(const QString &provider, const QString &host, const QString &aliases)
{
    const bool everySite = host == Account::kEverySite;
    QStringList hosts;
    // "old", or "old = site" (also → or ->), separated by commas or new lines
    for (const QString &entry : aliases.split(QRegularExpression(QStringLiteral("[,;\\n]+")), Qt::SkipEmptyParts)) {
        const QStringList parts = entry.split(QRegularExpression(QStringLiteral("\\s*(?:=|->|→)\\s*")));
        const QString old = Links::hostOf(parts.value(0));
        QString site = parts.size() > 1 ? Links::hostOf(parts.at(1)) : QString();
        if (old.isEmpty() || old.compare(host, Qt::CaseInsensitive) == 0)
            continue;
        // The token goes only where the account belongs: its own host, or
        // for every site an Atlassian one
        if (everySite) {
            if (!Links::atlassianSite(site))
                continue;
        } else if (site.compare(host, Qt::CaseInsensitive) == 0) {
            site.clear();
        } else if (!site.isEmpty()) {
            continue;
        }
        const QString stored = site.isEmpty() ? old : old + QLatin1Char('=') + site;
        if (!hosts.contains(stored))
            hosts << stored;
    }
    for (Account &a : m_accounts) {
        if (a.provider != provider || a.host.compare(host, Qt::CaseInsensitive) != 0 || a.aliases == hosts)
            continue;
        a.aliases = hosts;
        if (m_persistent)
            store();
        emit changed();
    }
}

QString Accounts::keyOf(const Account &account)
{
    return account.provider + QLatin1Char('/') + account.host.toLower();
}

void Accounts::token(const Account &account, const std::function<void(const QByteArray &)> &done)
{
    const QString key = keyOf(account);
    if (m_tokens.contains(key)) {
        done(m_tokens.value(key));
        return;
    }
    if (!m_persistent || !Keychain::available()) {
        done({});
        return;
    }
    // Asked once, however many links wait for it
    QList<std::function<void(const QByteArray &)>> &waiting = m_waiting[key];
    waiting.append(done);
    if (waiting.size() > 1)
        return;
    QPointer<Accounts> self(this);
    QThreadPool::globalInstance()->start([self, key] {
        QString error;
        const QByteArray token = Keychain::read(key, &error).value_or(QByteArray());
        if (!error.isEmpty())
            qWarning("Cache: the keychain has no token for %s: %s", qPrintable(key), qPrintable(error));
        QMetaObject::invokeMethod(
            self.get(),
            [self, key, token] {
                if (!self)
                    return;
                self->m_tokens.insert(key, token);
                const auto waiting = self->m_waiting.take(key);
                for (const auto &done : waiting)
                    done(token);
            },
            Qt::QueuedConnection);
    });
}

void Accounts::remember(const Account &account, const QByteArray &token)
{
    add(account);
    m_tokens.insert(keyOf(account), token);
}

void Accounts::save(const Account &account, const QByteArray &token,
                    const std::function<void(bool, const QString &)> &done)
{
    if (!m_persistent) {
        remember(account, token);
        done(true, {});
        return;
    }
    QPointer<Accounts> self(this);
    QThreadPool::globalInstance()->start([self, account, token, done] {
        QString error;
        const bool ok = Keychain::write(keyOf(account), token, &error);
        QMetaObject::invokeMethod(
            self.get(),
            [self, account, token, done, ok, error] {
                if (!self)
                    return;
                if (ok) {
                    self->remember(account, token);
                    self->store();
                }
                done(ok, error);
            },
            Qt::QueuedConnection);
    });
}

void Accounts::remove(const QString &provider, const QString &host)
{
    const std::optional<Account> account = find(provider, host);
    if (!account)
        return;
    const QString key = keyOf(*account);
    m_accounts.removeIf([&](const Account &a) { return keyOf(a) == key; });
    m_tokens.remove(key);
    if (m_persistent) {
        store();
        QThreadPool::globalInstance()->start([key] { Keychain::remove(key); });
    }
    emit changed();
}

void Accounts::add(const Account &account)
{
    const QString key = keyOf(account);
    for (Account &a : m_accounts) {
        if (keyOf(a) == key) {
            // Signing in again keeps the aliases and teams
            const QStringList aliases = a.aliases;
            const QStringList teams = a.teams;
            const QStringList libraries = a.libraries;
            a = account;
            if (a.aliases.isEmpty())
                a.aliases = aliases;
            if (a.teams.isEmpty())
                a.teams = teams;
            if (a.libraries.isEmpty())
                a.libraries = libraries;
            emit changed();
            return;
        }
    }
    m_accounts.append(account);
    emit changed();
}

void Accounts::store() const
{
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("accounts"), int(m_accounts.size()));
    for (int i = 0; i < m_accounts.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("provider"), m_accounts.at(i).provider);
        settings.setValue(QStringLiteral("host"), m_accounts.at(i).host);
        settings.setValue(QStringLiteral("user"), m_accounts.at(i).user);
        settings.setValue(QStringLiteral("aliases"), m_accounts.at(i).aliases);
        settings.setValue(QStringLiteral("teams"), m_accounts.at(i).teams);
        settings.setValue(QStringLiteral("libraries"), m_accounts.at(i).libraries);
    }
    settings.endArray();
}

// ---------------------------------------------------------------------------
// Links

namespace {

QString cacheFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/links.json");
}

} // namespace

Links::Links(QObject *parent)
    : QObject(parent)
    , m_persistent(true)
    , m_accounts(new Accounts(true, this))
{
    m_bump.setSingleShot(true);
    m_bump.setInterval(150);
    connect(&m_bump, &QTimer::timeout, this, [this] {
        ++m_revision;
        emit revisionChanged();
    });
    m_save.setSingleShot(true);
    m_save.setInterval(2000);
    connect(&m_save, &QTimer::timeout, this, &Links::save);
    m_imageFolder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/link-images");
    // Another account or alias: links may be read differently now
    connect(m_accounts, &Accounts::changed, this, &Links::touch);
    addResolver(std::make_unique<JiraResolver>(this));
    addResolver(std::make_unique<ConfluenceResolver>(this));
    addResolver(std::make_unique<FigmaResolver>(this));
    addResolver(std::make_unique<GitLabResolver>(this));
    addResolver(std::make_unique<GitHubResolver>(this));
    addResolver(std::make_unique<GerritResolver>(this));
    addOpenResolver(std::make_unique<WikipediaResolver>(this));
    m_page = std::make_unique<PageResolver>(this);
}

// What saved answers hold: raised when a resolver reads more than it
// did, so answers saved before are asked again (and shown till then)
constexpr int kAnswersVersion = 2;

void Links::save()
{
    QJsonObject all;
    all.insert(QStringLiteral("version"), kAnswersVersion);
    QJsonObject sites;
    for (auto it = m_moved.cbegin(); it != m_moved.cend(); ++it)
        sites.insert(it.key(), it.value());
    all.insert(QStringLiteral("sites"), sites);
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (!it->info)
            continue;
        all.insert(it.key(), QJsonObject{{QStringLiteral("fetched"), it->fetched.toString(Qt::ISODate)},
                                         {QStringLiteral("info"), QJsonObject::fromVariantMap(it->info->toVariant())}});
    }
    QDir().mkpath(QFileInfo(cacheFile()).absolutePath());
    QSaveFile file(cacheFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

Links::Links(Detached, QObject *parent)
    : QObject(parent)
    , m_persistent(false)
    , m_accounts(new Accounts(false, this))
{
    m_bump.setSingleShot(true);
    m_bump.setInterval(0);
    connect(&m_bump, &QTimer::timeout, this, [this] {
        ++m_revision;
        emit revisionChanged();
    });
    m_loaded = true;
    connect(m_accounts, &Accounts::changed, this, &Links::touch);
    addResolver(std::make_unique<JiraResolver>(this));
    addResolver(std::make_unique<ConfluenceResolver>(this));
    addResolver(std::make_unique<FigmaResolver>(this));
    addResolver(std::make_unique<GitLabResolver>(this));
    addResolver(std::make_unique<GitHubResolver>(this));
    addResolver(std::make_unique<GerritResolver>(this));
    addOpenResolver(std::make_unique<WikipediaResolver>(this));
    m_page = std::make_unique<PageResolver>(this);
}

Links::~Links()
{
    if (m_save.isActive())
        save();
}

QVariantList Links::providers() const
{
    QVariantList list;
    for (const auto &resolver : m_resolvers) {
        if (resolver->signsIn())
            list << QVariantMap{{QStringLiteral("id"), resolver->id()},
                                {QStringLiteral("name"), resolver->name()},
                                {QStringLiteral("fixedHost"), resolver->fixedHost()},
                                {QStringLiteral("needsUser"), resolver->needsUser()}};
    }
    return list;
}

QNetworkAccessManager *Links::network()
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    return m_network;
}

void Links::addResolver(std::unique_ptr<LinkResolver> resolver)
{
    m_resolvers.push_back(std::move(resolver));
}

void Links::addOpenResolver(std::unique_ptr<LinkResolver> resolver)
{
    m_open.push_back(std::move(resolver));
}

LinkResolver *Links::resolver(const QString &id) const
{
    for (const auto &r : m_resolvers) {
        if (r->id() == id)
            return r.get();
    }
    for (const auto &r : m_open) {
        if (r->id() == id)
            return r.get();
    }
    return nullptr;
}

LinkResolver *Links::resolverFor(const QUrl &url) const
{
    for (const auto &resolver : m_resolvers) {
        if (resolver->handles(url))
            return resolver.get();
    }
    return nullptr;
}

QVariantMap Links::info(const QString &url)
{
    if (!url.startsWith(QLatin1String("http")))
        return {};
    LinkResolver *resolver = resolverFor(QUrl(url));
    if (!resolver) {
        // An issue on a host no account reads: maybe another address of
        // a site one does (asked once); else how it could be read
        if (Markdown::issueKey(url).isEmpty() && !ConfluenceResolver::looksLike(QUrl(url)))
            return {};
        const QString host = authority(QUrl(url));
        load();
        if (!m_moved.contains(host) && !m_accounts->list().isEmpty()) {
            discover(host);
            return {};
        }
        return {{QStringLiteral("error"),
                 tr("No account reads %1. Add one in Settings, Accounts; or, if it is another address of an "
                    "Atlassian site, give your account it under Also for: \u201c%1 = yoursite.atlassian.net\u201d.")
                     .arg(host)}};
    }
    return lookUp(url, resolver);
}

QVariantMap Links::card(const QString &url)
{
    if (!url.startsWith(QLatin1String("http")))
        return {};
    LinkResolver *resolver = resolverFor(QUrl(url));
    // An issue or page on an address yet to be looked up: maybe a site's
    if (!resolver && (!Markdown::issueKey(url).isEmpty() || ConfluenceResolver::looksLike(QUrl(url)))) {
        const QString host = authority(QUrl(url));
        load();
        if (!m_moved.contains(host) && !m_accounts->list().isEmpty()) {
            discover(host);
            return {};
        }
    }
    if (!resolver) {
        for (const auto &open : m_open) {
            if (open->handles(QUrl(url))) {
                resolver = open.get();
                break;
            }
        }
    }
    QVariantMap info = lookUp(url, resolver ? resolver : m_page.get());
    if (!info.isEmpty())
        info.insert(QStringLiteral("source"), resolver ? resolver->id() : QStringLiteral("page"));
    return info;
}

QStringList Links::cardFields(const QString &source) const
{
    return QSettings().value(QStringLiteral("linkCards/") + source).toStringList();
}

void Links::setCardFields(const QString &source, const QStringList &fields)
{
    if (source.isEmpty())
        return;
    QSettings settings;
    if (fields.isEmpty())
        settings.remove(QStringLiteral("linkCards/") + source);
    else
        settings.setValue(QStringLiteral("linkCards/") + source, fields);
    touch();
}

void Links::reload(const QString &url)
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it.key().section(QLatin1Char(' '), 1) == url) {
            it->fetched = {};
            it->failed = {};
        }
    }
    touch();
}

QVariantMap Links::lookUp(const QString &url, LinkResolver *resolver)
{
    if (!resolver)
        return {};
    load();
    const QString key = resolver->id() + QLatin1Char(' ') + url;
    Entry &entry = m_entries[key];
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool fresh = entry.info && entry.fetched.isValid() && entry.fetched.secsTo(now) < kFreshSeconds;
    const bool resting = entry.failed.isValid() && entry.failed.secsTo(now) < kRetrySeconds;
    if (!fresh && !resting && !entry.pending) {
        entry.pending = true;
        QPointer<Links> self(this);
        resolver->resolve(QUrl(url), [self, key](const std::optional<LinkInfo> &info, const QString &error) {
            if (!self)
                return;
            self->m_sharpened.remove(key); // read anew: may grow again
            self->settle(key, info, error);
        });
    }
    if (entry.info)
        return entry.info->toVariant();
    if (!entry.error.isEmpty())
        return {{QStringLiteral("error"), entry.error}};
    return {};
}

void Links::settle(const QString &key, const std::optional<LinkInfo> &info, const QString &error)
{
    Entry &entry = m_entries[key];
    entry.pending = false;
    if (!info) {
        // What was known stays shown; asked again in a while
        entry.failed = QDateTime::currentDateTimeUtc();
        entry.error = error;
        if (!error.isEmpty())
            qWarning("Cache: no details for %s: %s", qPrintable(key.section(QLatin1Char(' '), 1)), qPrintable(error));
        m_bump.start(); // a card stops waiting
        return;
    }
    LinkInfo got = *info;
    // A picture from the web: shown from disk from now on. Until a new one
    // is there, the one there was stays.
    const bool web = got.image.startsWith(QLatin1String("http"));
    if (web && !m_imageFolder.isEmpty()) {
        const QString remote = got.image;
        got.remoteImage = remote;
        const bool kept = entry.info && entry.info->remoteImage == remote
                          && QFileInfo::exists(QUrl(entry.info->image).toLocalFile());
        if (kept)
            got.image = entry.info->image;
        else if (entry.info && entry.info->image.startsWith(QLatin1String("file:")))
            got.image = entry.info->image;
        if (!kept)
            keepImage(key, remote);
    }
    entry.info = got;
    entry.fetched = QDateTime::currentDateTimeUtc();
    entry.failed = {};
    entry.error.clear();
    if (m_persistent)
        m_save.start();
    m_bump.start();
}

void Links::sharpen(const QString &url, int pixels)
{
    // In steps, so a little more zoom doesn't ask again
    int step = 4096;
    for (const int s : {800, 1600, 3200}) {
        if (pixels <= s) {
            step = s;
            break;
        }
    }
    LinkResolver *resolver = resolverFor(QUrl(url));
    if (!resolver)
        return;
    const QString key = resolver->id() + QLatin1Char(' ') + url;
    const auto it = m_entries.constFind(key);
    // Asked once per step: a render can't grow past its service's limit
    if (it == m_entries.cend() || !it->info || it->pending || it->info->renderSize <= 0
        || it->info->renderSize >= step || m_sharpening.contains(key) || m_sharpened.value(key) >= step)
        return;
    m_sharpening.insert(key);
    m_sharpened[key] = step;
    QPointer<Links> self(this);
    const bool asked = resolver->sharpen(QUrl(url), step, [self, key](const std::optional<LinkInfo> &info, const QString &) {
        if (!self)
            return;
        self->m_sharpening.remove(key);
        if (info)
            self->settle(key, info, {});
    });
    if (!asked)
        m_sharpening.remove(key);
}

void Links::addAccount(const QString &provider, const QString &host, const QString &user, const QString &token,
                       bool everySite)
{
    LinkResolver *resolver = this->resolver(provider);
    const QString h = resolver && !resolver->fixedHost().isEmpty() ? resolver->fixedHost() : hostOf(host);
    everySite = everySite && atlassianSite(h);
    if (!resolver || h.isEmpty() || token.trimmed().isEmpty()) {
        emit accountAdded(false, tr("A host and a token are needed."));
        return;
    }
    // Signed in on the site given; kept for it, or for every site
    const Account site{provider, h, user.trimmed(), {}};
    const Account account{provider, everySite ? Account::kEverySite : h, user.trimmed(), {}};
    const QByteArray secret = token.trimmed().toUtf8();
    QPointer<Links> self(this);
    resolver->signIn(site, secret, [self, account, secret](bool ok, const QString &signedIn) {
        if (!self)
            return;
        if (!ok) {
            emit self->accountAdded(false, signedIn);
            return;
        }
        const QString message = account.everySite()
            ? signedIn + QLatin1Char(' ') + tr("Links to every atlassian.net site are read with it.")
            : signedIn;
        self->m_accounts->save(account, secret, [self, message](bool saved, const QString &error) {
            if (!self)
                return;
            if (!saved) {
                emit self->accountAdded(false, tr("Signed in, but the keychain didn't keep the token: %1").arg(error));
                return;
            }
            // Links that couldn't be read before may be now
            for (Entry &entry : self->m_entries)
                entry.failed = {};
            self->touch();
            emit self->accountAdded(true, message);
        });
    });
}

std::optional<std::pair<Account, QString>> Links::accountFor(const QString &provider, const QUrl &url) const
{
    const QString host = authority(url);
    for (const QString &h : {host, movedTo(host)}) {
        if (h.isEmpty())
            continue;
        if (const std::optional<Account> a = m_accounts->find(provider, h)) {
            const QString site = a->siteFor(h);
            if (!site.isEmpty())
                return std::make_pair(*a, site);
        }
    }
    return std::nullopt;
}

bool Links::secure(const QUrl &url)
{
    return url.scheme() == QLatin1String("https")
           || (url.scheme() == QLatin1String("http") && loopback(authority(url)));
}

void Links::request(const QString &site, const Account &account, const QByteArray &token, const QString &path,
                    const JsonReply &reply)
{
    requestBytes(site, account, token, path, [reply](int status, const QByteArray &body, const QString &error) {
        reply(status, QJsonDocument::fromJson(body).object(), error);
    });
}

void Links::requestBytes(const QString &site, const Account &account, const QByteArray &token, const QString &path,
                         const BytesReply &reply)
{
    QNetworkRequest request(QUrl(QStringLiteral("%1://%2%3")
                                     .arg(loopback(site) ? QStringLiteral("http") : QStringLiteral("https"), site, path)));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", account.user.isEmpty()
                                              ? "Bearer " + token
                                              : "Basic " + (account.user.toUtf8() + ':' + token).toBase64());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Cache"));
    request.setTransferTimeout(15000);
    QNetworkReply *r = network()->get(request);
    QObject::connect(r, &QNetworkReply::finished, r, [r, reply] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = r->readAll();
        const QJsonObject json = QJsonDocument::fromJson(body).object();
        // Jira's errorMessages, Confluence's and GitLab's message, or the network's
        QString error = r->errorString();
        const QJsonArray messages = json.value(QLatin1String("errorMessages")).toArray();
        if (!messages.isEmpty())
            error = messages.first().toString();
        else if (json.value(QLatin1String("message")).isString())
            error = json.value(QLatin1String("message")).toString();
        else if (json.value(QLatin1String("error")).isString())
            error = json.value(QLatin1String("error")).toString();
        reply(status, body, error);
    });
}

QString Links::highlight(const QString &code, const QString &language, const QVariantMap &colors) const
{
    return QStringLiteral("<pre style=\"margin:0; white-space:pre;\">")
           + Highlight::toHtml(code, Highlight::language(language, code), colors) + QStringLiteral("</pre>");
}

void Links::discover(const QString &host)
{
    if (m_discovering.contains(host) || (!loopback(host) && QUrl(QStringLiteral("https://") + host).host().isEmpty()))
        return;
    m_discovering.insert(host);
    const QString scheme = loopback(host) ? QStringLiteral("http") : QStringLiteral("https");
    QPointer<Links> self(this);
    // Asked without signing in: nothing of the user's goes to `host`
    const auto ask = [self](const QString &url, const std::function<void(const QJsonObject &)> &done) {
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("Accept", "application/json");
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Cache"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(10000);
        QNetworkReply *reply = self->network()->get(request);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            done(status == 200 ? QJsonDocument::fromJson(reply->read(64 * 1024)).object() : QJsonObject());
        });
    };
    const auto settle = [self, host](const QString &site) {
        if (!self)
            return;
        self->m_discovering.remove(host);
        self->m_moved.insert(host, site);
        if (self->m_persistent)
            self->m_save.start();
        self->touch();
    };
    ask(scheme + QStringLiteral("://") + host + QStringLiteral("/rest/api/2/serverInfo"),
        [self, ask, settle, scheme, host](const QJsonObject &info) {
            if (!self)
                return;
            const QUrl base(info.value(QLatin1String("baseUrl")).toString());
            const QString site = authority(base);
            const bool cloud = info.value(QLatin1String("deploymentType")).toString() == QLatin1String("Cloud");
            if (!cloud || site.isEmpty() || site == host || !(atlassianSite(site) || loopback(site))) {
                settle({});
                return;
            }
            // The same tenant at both addresses
            ask(scheme + QStringLiteral("://") + host + QStringLiteral("/_edge/tenant_info"),
                [self, ask, settle, base, site](const QJsonObject &here) {
                    const QString id = here.value(QLatin1String("cloudId")).toString();
                    if (!self || id.isEmpty()) {
                        settle({});
                        return;
                    }
                    ask(base.scheme() + QStringLiteral("://") + site + QStringLiteral("/_edge/tenant_info"),
                        [settle, site, id](const QJsonObject &there) {
                            settle(there.value(QLatin1String("cloudId")).toString() == id ? site : QString());
                        });
                });
        });
}

void Links::search(const QString &query, const QStringList &sources)
{
    if (query.trimmed().size() < 3)
        return;
    load(); // the sites links were read from
    QPointer<Links> self(this);
    for (const auto &resolver : m_resolvers) {
        const QString source = resolver->id();
        if (!sources.isEmpty() && !sources.contains(source))
            continue;
        resolver->search(query.trimmed(), [self, query, source](const QVariantList &results) {
            if (self)
                emit self->found(query, source, results);
        });
    }
}

QString Links::issueKey(const QString &url) const
{
    return Markdown::issueKey(url);
}

bool Links::canRelate(const QString &url) const
{
    if (Markdown::issueKey(url).isEmpty())
        return false;
    return std::any_of(m_resolvers.cbegin(), m_resolvers.cend(), [this](const auto &resolver) {
        return resolver->relates() && std::any_of(m_accounts->all().cbegin(), m_accounts->all().cend(),
                                                  [&](const Account &a) { return a.provider == resolver->id(); });
    });
}

void Links::related(const QString &url)
{
    const QString key = Markdown::issueKey(url);
    if (!canRelate(url)) {
        emit relatedFound(url, key, {});
        return;
    }
    struct Gather {
        QVariantList results;
        int pending = 1;
    };
    auto gather = std::make_shared<Gather>();
    QPointer<Links> self(this);
    const auto settle = [self, gather, url, key] {
        if (--gather->pending == 0 && self)
            emit self->relatedFound(url, key, gather->results);
    };
    for (const auto &resolver : m_resolvers) {
        if (!resolver->relates())
            continue;
        ++gather->pending;
        resolver->related(key, [gather, settle](const QVariantList &found) {
            gather->results += found;
            settle();
        });
    }
    settle();
}

void Links::refreshReviews()
{
    // Each service with an account answers; together they are the list
    struct Gather {
        QVariantList reviews;
        int pending = 1;
    };
    auto gather = std::make_shared<Gather>();
    QPointer<Links> self(this);
    const auto settle = [self, gather] {
        if (--gather->pending > 0 || !self || self->m_reviews == gather->reviews)
            return;
        self->m_reviews = gather->reviews;
        emit self->reviewsChanged();
    };
    for (const auto &resolver : m_resolvers) {
        const QString id = resolver->id();
        const bool any = std::any_of(m_accounts->all().cbegin(), m_accounts->all().cend(),
                                     [&id](const Account &a) { return a.provider == id; });
        if (!any)
            continue;
        ++gather->pending;
        resolver->reviews([gather, settle](const QVariantList &found) {
            gather->reviews += found;
            settle();
        });
    }
    settle();
}

QStringList Links::atlassianSites() const
{
    QStringList sites;
    bool everySite = false;
    for (const Account &a : m_accounts->all()) {
        if (a.provider != QLatin1String("jira"))
            continue;
        if (a.everySite())
            everySite = true;
        else if (!sites.contains(a.host))
            sites << a.host;
    }
    if (everySite) {
        // The sites links have been read from, and those custom domains are of
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            const QString host = authority(QUrl(it.key().section(QLatin1Char(' '), 1)));
            if (atlassianSite(host) && !sites.contains(host))
                sites << host;
        }
        for (const QString &site : m_moved) {
            if (atlassianSite(site) && !sites.contains(site))
                sites << site;
        }
    }
    return sites;
}

std::optional<LinkInfo> Links::known(const QString &resolver, const QString &url) const
{
    const auto it = m_entries.constFind(resolver + QLatin1Char(' ') + url);
    return it != m_entries.cend() ? it->info : std::nullopt;
}

void Links::keepImage(const QString &key, const QString &remote)
{
    if (m_downloading.contains(remote))
        return;
    m_downloading.insert(remote);
    QNetworkRequest request{QUrl(remote)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Cache"));
    request.setTransferTimeout(30000);
    QNetworkReply *reply = network()->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > 20 * 1024 * 1024)
            reply->abort(); // not a picture for a card
    });
    QPointer<Links> self(this);
    QObject::connect(reply, &QNetworkReply::finished, reply, [self, reply, key, remote] {
        reply->deleteLater();
        if (!self)
            return;
        self->m_downloading.remove(remote);
        const QByteArray data = reply->readAll();
        auto it = self->m_entries.find(key);
        // Still the picture it shows, and one at all
        if (reply->error() != QNetworkReply::NoError || data.isEmpty() || it == self->m_entries.end() || !it->info
            || it->info->remoteImage != remote)
            return;
        // Named after the link and the picture: a new one is a new file, so
        // nothing shows a stale copy of the old
        const QByteArray name = QCryptographicHash::hash((key + QLatin1Char('\n') + remote).toUtf8(),
                                                         QCryptographicHash::Sha1).toHex();
        QDir().mkpath(self->m_imageFolder);
        const QString file = QDir(self->m_imageFolder).filePath(QString::fromLatin1(name));
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
            return;
        // The one it showed before goes
        const QString old = QUrl(it->info->image).toLocalFile();
        if (!old.isEmpty() && old != file && old.startsWith(self->m_imageFolder))
            QFile::remove(old);
        it->info->image = QUrl::fromLocalFile(file).toString();
        if (self->m_persistent)
            self->m_save.start();
        self->m_bump.start();
    });
}

void Links::touch()
{
    m_bump.start();
}

QString Links::hostOf(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return {};
    const QUrl url = QUrl::fromUserInput(t.contains(QLatin1String("://")) ? t : QStringLiteral("https://") + t);
    return url.isValid() ? authority(url) : QString();
}

QString Links::authority(const QUrl &url)
{
    const QString host = url.host().toLower();
    return url.port() > 0 ? host + QLatin1Char(':') + QString::number(url.port()) : host;
}

bool Links::atlassianSite(const QString &host)
{
    static const QRegularExpression site(QStringLiteral(R"(^[a-z0-9][a-z0-9-]*\.atlassian\.net$)"));
    return site.match(host.toLower()).hasMatch();
}

bool Links::loopback(const QString &host)
{
    const QString name = host.section(QLatin1Char(':'), 0, 0);
    return name == QLatin1String("127.0.0.1") || name == QLatin1String("localhost");
}

void Links::load()
{
    if (m_loaded)
        return;
    m_loaded = true;
    QFile file(cacheFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject all = QJsonDocument::fromJson(file.readAll()).object();
    // Saved by a Cache that read less: shown, but asked again at once
    const bool outdated = all.value(QStringLiteral("version")).toInt() != kAnswersVersion;
    const QJsonObject sites = all.value(QStringLiteral("sites")).toObject();
    for (auto it = sites.begin(); it != sites.end(); ++it)
        m_moved.insert(it.key(), it.value().toString());
    for (auto it = all.begin(); it != all.end(); ++it) {
        if (!it.key().contains(QLatin1Char(' ')))
            continue; // from before entries were kept by resolver
        const QJsonObject o = it.value().toObject();
        Entry entry;
        entry.info = LinkInfo::fromVariant(o.value(QStringLiteral("info")).toObject().toVariantMap());
        if (!outdated)
            entry.fetched = QDateTime::fromString(o.value(QStringLiteral("fetched")).toString(), Qt::ISODate);
        m_entries.insert(it.key(), entry);
    }
}
