#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

// Secrets in the system's own store: the Secret Service (GNOME Keyring,
// KWallet) through libsecret on Linux, the Keychain on macOS, the
// Credential Manager on Windows. Each secret is kept under a key of the
// caller's ("jira/example.atlassian.net"), for the service "Cache".
//
// Every call blocks, and may wait for the user to unlock the store: call
// them off the GUI thread.
namespace Keychain {

// Whether this build has a store to keep secrets in (Linux needs libsecret)
bool available();

bool write(const QString &key, const QByteArray &secret, QString *error = nullptr);
// Nothing (and no error) when there is no secret under `key`
std::optional<QByteArray> read(const QString &key, QString *error = nullptr);
bool remove(const QString &key, QString *error = nullptr);

} // namespace Keychain
