#include "links.h"

#include "jiraresolver.h"
#include "keychain.h"
#include "pageresolver.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
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
            {QStringLiteral("site"), site}};
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
    for (const QVariant &row : map.value(QStringLiteral("fields")).toList()) {
        const QVariantMap r = row.toMap();
        info.fields.append({r.value(QStringLiteral("name")).toString(), r.value(QStringLiteral("value")).toString()});
    }
    return info;
}

// ---------------------------------------------------------------------------
// Accounts

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
                        settings.value(QStringLiteral("user")).toString()};
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
                            {QStringLiteral("user"), a.user}};
    return list;
}

bool Accounts::keychain() const
{
    return Keychain::available();
}

std::optional<Account> Accounts::find(const QString &provider, const QString &host) const
{
    for (const Account &a : m_accounts) {
        if (a.provider == provider && a.host.compare(host, Qt::CaseInsensitive) == 0)
            return a;
    }
    return std::nullopt;
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
            a = account;
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
    addResolver(std::make_unique<JiraResolver>(this));
    m_page = std::make_unique<PageResolver>(this);
}

void Links::save()
{
    QJsonObject all;
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
    addResolver(std::make_unique<JiraResolver>(this));
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
    for (const auto &resolver : m_resolvers)
        list << QVariantMap{{QStringLiteral("id"), resolver->id()}, {QStringLiteral("name"), resolver->name()}};
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
    // Nothing shown for hosts without an account (any more)
    return lookUp(url, resolverFor(QUrl(url)));
}

QVariantMap Links::card(const QString &url)
{
    if (!url.startsWith(QLatin1String("http")))
        return {};
    LinkResolver *resolver = resolverFor(QUrl(url));
    return lookUp(url, resolver ? resolver : m_page.get());
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
            Entry &entry = self->m_entries[key];
            entry.pending = false;
            if (info) {
                entry.info = info;
                entry.fetched = QDateTime::currentDateTimeUtc();
                entry.failed = {};
                if (self->m_persistent)
                    self->m_save.start();
                self->m_bump.start();
            } else {
                // What was known stays shown; asked again in a while
                entry.failed = QDateTime::currentDateTimeUtc();
                if (!error.isEmpty())
                    qWarning("Cache: no details for %s: %s", qPrintable(key.section(QLatin1Char(' '), 1)),
                             qPrintable(error));
                self->m_bump.start(); // a card stops waiting
            }
        });
    }
    return entry.info ? entry.info->toVariant() : QVariantMap();
}

void Links::addAccount(const QString &provider, const QString &host, const QString &user, const QString &token)
{
    const QString h = hostOf(host);
    LinkResolver *resolver = nullptr;
    for (const auto &r : m_resolvers) {
        if (r->id() == provider)
            resolver = r.get();
    }
    if (!resolver || h.isEmpty() || token.trimmed().isEmpty()) {
        emit accountAdded(false, tr("A host and a token are needed."));
        return;
    }
    const Account account{provider, h, user.trimmed()};
    const QByteArray secret = token.trimmed().toUtf8();
    QPointer<Links> self(this);
    resolver->signIn(account, secret, [self, account, secret](bool ok, const QString &message) {
        if (!self)
            return;
        if (!ok) {
            emit self->accountAdded(false, message);
            return;
        }
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
    for (auto it = all.begin(); it != all.end(); ++it) {
        if (!it.key().contains(QLatin1Char(' ')))
            continue; // from before entries were kept by resolver
        const QJsonObject o = it.value().toObject();
        Entry entry;
        entry.info = LinkInfo::fromVariant(o.value(QStringLiteral("info")).toObject().toVariantMap());
        entry.fetched = QDateTime::fromString(o.value(QStringLiteral("fetched")).toString(), Qt::ISODate);
        m_entries.insert(it.key(), entry);
    }
}
