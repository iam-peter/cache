#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Versions of the projects, kept with git. The workspace folder becomes a
// repository of its own (never one it happens to lie in); a while after
// a project is saved its changes are committed, so every project can go
// back to how it was at any earlier commit. Needs git on the PATH; without
// it, available is false and nothing happens.
class History : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit History(QObject *parent = nullptr);
    ~History() override;

    bool available() const { return !m_git.isEmpty(); }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // Commits the project's changes in a while; again calls restart the wait
    Q_INVOKABLE void schedule(const QString &workspaceRoot, const QString &project);
    // Commits what is waiting now
    Q_INVOKABLE void flush();
    // The project's versions, newest first: sha, time (ms since the epoch),
    // subject
    Q_INVOKABLE QVariantList versions(const QString &workspaceRoot, const QString &project);
    // Puts the project back as it was at `sha`, after committing how it is
    // now, so a restore can be undone by restoring again. Close the
    // project first and load it again afterwards.
    Q_INVOKABLE bool restore(const QString &workspaceRoot, const QString &project, const QString &sha);

    // For tests: commit right away
    bool commit(const QString &workspaceRoot, const QString &project, const QString &message);

signals:
    void enabledChanged();
    void committed(const QString &project);

private:
    bool run(const QString &root, const QStringList &args, QByteArray *output = nullptr, int *exitCode = nullptr) const;
    bool ensureRepository(const QString &root) const;

    QString m_git;
    bool m_enabled = true;
    QTimer m_timer;
    QString m_pendingRoot;
    QStringList m_pendingProjects;
};
