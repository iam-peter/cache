#include "history.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

History::History(QObject *parent)
    : QObject(parent)
    , m_git(QStandardPaths::findExecutable(QStringLiteral("git")))
{
    m_enabled = QSettings().value(QStringLiteral("history/enabled"), true).toBool();
    m_timer.setSingleShot(true);
    m_timer.setInterval(20 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &History::flush);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &History::flush);
}

History::~History()
{
    flush();
}

void History::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    QSettings().setValue(QStringLiteral("history/enabled"), enabled);
    emit enabledChanged();
}

bool History::run(const QString &root, const QStringList &args, QByteArray *output, int *exitCode) const
{
    if (m_git.isEmpty())
        return false;
    QProcess git;
    git.setWorkingDirectory(root);
    git.setProcessChannelMode(QProcess::SeparateChannels);
    // Never ask for anything, never page
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    git.setProcessEnvironment(env);
    git.start(m_git, QStringList{QStringLiteral("-C"), root} + args);
    if (!git.waitForFinished(15000)) {
        git.kill();
        return false;
    }
    if (output)
        *output = git.readAllStandardOutput();
    if (exitCode)
        *exitCode = git.exitCode();
    return git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
}

// The workspace is a repository of its own, also when it lies inside
// another one: notes are never committed to someone else's repository
bool History::ensureRepository(const QString &root) const
{
    QByteArray top;
    if (run(root, {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, &top)
        && QFileInfo(QString::fromUtf8(top.trimmed())).canonicalFilePath() == QFileInfo(root).canonicalFilePath())
        return true;
    return run(root, {QStringLiteral("init"), QStringLiteral("-q")});
}

bool History::commit(const QString &root, const QString &project, const QString &message)
{
    if (m_git.isEmpty() || root.isEmpty() || project.isEmpty() || !QFileInfo(QDir(root).filePath(project)).isDir())
        return false;
    if (!ensureRepository(root))
        return false;
    const QString path = project + QLatin1Char('/');
    run(root, {QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--"), path});
    int changed = 0;
    run(root, {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--quiet"), QStringLiteral("--"), path},
        nullptr, &changed);
    if (changed == 0)
        return true; // nothing new

    // The user's own name if git knows it, else the app's
    QStringList identity;
    QByteArray email;
    if (!run(root, {QStringLiteral("config"), QStringLiteral("user.email")}, &email) || email.trimmed().isEmpty()) {
        identity = {QStringLiteral("-c"), QStringLiteral("user.name=Note Canvas"),
                    QStringLiteral("-c"), QStringLiteral("user.email=notecanvas@localhost")};
    }
    // The app's own repository: no signing prompts, no hooks
    identity += {QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false")};
    const bool ok = run(root, identity + QStringList{QStringLiteral("commit"), QStringLiteral("-q"),
                                                       QStringLiteral("--no-verify"), QStringLiteral("-m"), message,
                                                       QStringLiteral("--"), path});
    if (ok)
        emit committed(project);
    return ok;
}

void History::schedule(const QString &workspaceRoot, const QString &project)
{
    if (!m_enabled || m_git.isEmpty() || project.isEmpty())
        return;
    if (m_pendingRoot != workspaceRoot)
        flush();
    m_pendingRoot = workspaceRoot;
    if (!m_pendingProjects.contains(project))
        m_pendingProjects << project;
    m_timer.start();
}

void History::flush()
{
    m_timer.stop();
    const QStringList projects = std::exchange(m_pendingProjects, {});
    for (const QString &project : projects)
        commit(m_pendingRoot, project, QStringLiteral("Update %1").arg(project));
}

QVariantList History::versions(const QString &workspaceRoot, const QString &project)
{
    flush();
    QVariantList list;
    QByteArray out;
    if (m_git.isEmpty() || !run(workspaceRoot, {QStringLiteral("log"), QStringLiteral("-n"), QStringLiteral("300"),
                                                 QStringLiteral("--format=%H%x09%ct%x09%s"), QStringLiteral("--"),
                                                 project + QLatin1Char('/')},
                                &out))
        return list;
    for (const QByteArray &line : out.split('\n')) {
        const QList<QByteArray> parts = line.split('\t');
        if (parts.size() < 3)
            continue;
        list.append(QVariantMap{
            {QStringLiteral("sha"), QString::fromLatin1(parts.at(0))},
            {QStringLiteral("time"), parts.at(1).toLongLong() * 1000},
            {QStringLiteral("subject"), QString::fromUtf8(parts.at(2))},
        });
    }
    return list;
}

bool History::restore(const QString &workspaceRoot, const QString &project, const QString &sha)
{
    if (m_git.isEmpty() || sha.isEmpty())
        return false;
    flush();
    commit(workspaceRoot, project, QStringLiteral("Update %1").arg(project));
    const QString path = project + QLatin1Char('/');
    // Without overlay: files added since that version go away too
    if (!run(workspaceRoot, {QStringLiteral("restore"), QStringLiteral("--source"), sha, QStringLiteral("--staged"),
                             QStringLiteral("--worktree"), QStringLiteral("--"), path}))
        return false;
    QByteArray when;
    run(workspaceRoot, {QStringLiteral("show"), QStringLiteral("-s"), QStringLiteral("--format=%ci"), sha}, &when);
    commit(workspaceRoot, project,
           QStringLiteral("Restore %1 to %2").arg(project, QString::fromUtf8(when.trimmed()).left(16)));
    return true;
}
