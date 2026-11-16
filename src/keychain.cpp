#include "keychain.h"

#if defined(CACHE_HAVE_LIBSECRET)
// GLib's headers have members named "signals", which Qt defines away
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#endif

namespace {

const char kService[] = "Cache";

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

#if defined(CACHE_HAVE_LIBSECRET)

const SecretSchema *schema()
{
    static const SecretSchema s = {
        "io.github.iam-peter.cache.Secret",
        SECRET_SCHEMA_NONE,
        {{"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
         {"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
         {nullptr, SecretSchemaAttributeType(0)}},
        0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    return &s;
}

QString takeError(GError *error)
{
    const QString message = error ? QString::fromUtf8(error->message) : QStringLiteral("unknown error");
    if (error)
        g_error_free(error);
    return message;
}

#elif defined(Q_OS_MACOS)

// A query for the generic password under `key`; the caller releases it
CFMutableDictionaryRef query(const QString &key)
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = QString::fromLatin1(kService).toCFString();
    CFStringRef account = key.toCFString();
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, service);
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return q;
}

QString statusText(OSStatus status)
{
    CFStringRef text = SecCopyErrorMessageString(status, nullptr);
    const QString message = text ? QString::fromCFString(text) : QStringLiteral("error %1").arg(status);
    if (text)
        CFRelease(text);
    return message;
}

#elif defined(Q_OS_WIN)

std::wstring target(const QString &key)
{
    return (QString::fromLatin1(kService) + QLatin1Char('/') + key).toStdWString();
}

QString lastError()
{
    return QStringLiteral("error %1").arg(GetLastError());
}

#endif

} // namespace

namespace Keychain {

bool available()
{
#if defined(CACHE_HAVE_LIBSECRET) || defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

bool write(const QString &key, const QByteArray &secret, QString *error)
{
#if defined(CACHE_HAVE_LIBSECRET)
    GError *gerror = nullptr;
    const QByteArray label = QStringLiteral("Cache: %1").arg(key).toUtf8();
    const QByteArray value = secret + '\0';
    const bool ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, label.constData(),
                                               value.constData(), nullptr, &gerror, "service", kService,
                                               "key", key.toUtf8().constData(), nullptr);
    if (!ok)
        setError(error, takeError(gerror));
    return ok;
#elif defined(Q_OS_MACOS)
    CFMutableDictionaryRef q = query(key);
    SecItemDelete(q); // replaced, not added twice
    CFDataRef data = secret.toCFData();
    CFDictionarySetValue(q, kSecValueData, data);
    const OSStatus status = SecItemAdd(q, nullptr);
    CFRelease(data);
    CFRelease(q);
    if (status != errSecSuccess)
        setError(error, statusText(status));
    return status == errSecSuccess;
#elif defined(Q_OS_WIN)
    std::wstring name = target(key);
    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = name.data();
    credential.CredentialBlobSize = DWORD(secret.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const bool ok = CredWriteW(&credential, 0);
    if (!ok)
        setError(error, lastError());
    return ok;
#else
    Q_UNUSED(key)
    Q_UNUSED(secret)
    setError(error, QStringLiteral("this build has no keychain"));
    return false;
#endif
}

std::optional<QByteArray> read(const QString &key, QString *error)
{
#if defined(CACHE_HAVE_LIBSECRET)
    GError *gerror = nullptr;
    gchar *value = secret_password_lookup_sync(schema(), nullptr, &gerror, "service", kService, "key",
                                               key.toUtf8().constData(), nullptr);
    if (gerror) {
        setError(error, takeError(gerror));
        return std::nullopt;
    }
    if (!value)
        return std::nullopt;
    const QByteArray secret(value);
    secret_password_free(value);
    return secret;
#elif defined(Q_OS_MACOS)
    CFMutableDictionaryRef q = query(key);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(q, &result);
    CFRelease(q);
    if (status == errSecItemNotFound)
        return std::nullopt;
    if (status != errSecSuccess || !result) {
        setError(error, statusText(status));
        return std::nullopt;
    }
    const QByteArray secret = QByteArray::fromCFData(static_cast<CFDataRef>(result));
    CFRelease(result);
    return secret;
#elif defined(Q_OS_WIN)
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target(key).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        if (GetLastError() != ERROR_NOT_FOUND)
            setError(error, lastError());
        return std::nullopt;
    }
    const QByteArray secret(reinterpret_cast<const char *>(credential->CredentialBlob),
                            qsizetype(credential->CredentialBlobSize));
    CredFree(credential);
    return secret;
#else
    Q_UNUSED(key)
    setError(error, QStringLiteral("this build has no keychain"));
    return std::nullopt;
#endif
}

bool remove(const QString &key, QString *error)
{
#if defined(CACHE_HAVE_LIBSECRET)
    GError *gerror = nullptr;
    secret_password_clear_sync(schema(), nullptr, &gerror, "service", kService, "key", key.toUtf8().constData(),
                               nullptr);
    if (gerror) {
        setError(error, takeError(gerror));
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    CFMutableDictionaryRef q = query(key);
    const OSStatus status = SecItemDelete(q);
    CFRelease(q);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        setError(error, statusText(status));
        return false;
    }
    return true;
#elif defined(Q_OS_WIN)
    if (!CredDeleteW(target(key).c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) {
        setError(error, lastError());
        return false;
    }
    return true;
#else
    Q_UNUSED(key)
    setError(error, QStringLiteral("this build has no keychain"));
    return false;
#endif
}

} // namespace Keychain
