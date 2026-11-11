#include "workspace.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

namespace {

const char kWelcome[] = R"(# Welcome to Cache

An **infinite canvas** of Markdown notes. Each note is a plain `.md` file in this project's `notes/` folder.

- **Double-click** empty space for a note, double-click a note to edit it, `Esc` to finish
- **Drag** empty space to select several; hold `Space` (or use the hand) to pan; **scroll** to zoom
- The tools at the bottom add text, code, shapes, sections, connectors and pen lines
)";

const char kMedia[] = R"(## Images, videos and sharing

Images and videos live inside notes: **paste** or **drop** them on the canvas, or onto a note, or into one you are editing.

**Copy note** (`Ctrl+C`) puts a note on the clipboard *with its images*, ready for Teams or mail. `Ctrl+Shift+C` copies the selection as a picture.

```python
def hello():
    return "code blocks are highlighted"
```
)";

const char kLinks[] = R"(## Links and mindmaps

- Type `[[` in a note to link another one, in any project: [[Welcome to Cache]]
- `![[Title]]` shows a preview of the linked note
- Drag a note's side handle onto another note to connect them, or onto empty space for a new one
- `Tab` adds a child, `Enter` a sibling: a mindmap in seconds

- [ ] Try ticking this task; open tasks of all projects are in the sidebar
)";

const char kShortcuts[] = R"(## Shortcuts

| Key | Action |
|---|---|
| V H | Select, pan |
| N T K | Note, text, code |
| R O D S | Shapes, section |
| L P M | Connector, pen, highlighter |
| Tab / Enter | Child / sibling |
| Ctrl+C | Copy for Teams |
| Ctrl+Shift+T | Templates |
| F1 | All shortcuts |
)";

QString startRoot;

} // namespace

void Workspace::setStartRoot(const QString &path)
{
    startRoot = path;
}

Workspace::Workspace(QObject *parent)
    : QAbstractListModel(parent)
{
    QSettings settings;
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                             + QStringLiteral("/Cache");
    m_root = !startRoot.isEmpty() ? startRoot : settings.value(QStringLiteral("workspace/root"), fallback).toString();
    QDir().mkpath(m_root);
    scan();
    ensureProject();

    const QString last = settings.value(QStringLiteral("workspace/lastProject")).toString();
    QString path = m_projects.first().path;
    for (const Project &p : std::as_const(m_projects)) {
        if (p.name == last)
            path = p.path;
    }
    m_currentPath = path;
}

QString Workspace::rootDisplayPath() const
{
#ifndef Q_OS_WIN
    const QString home = QDir::homePath();
    if (m_root == home)
        return QStringLiteral("~");
    if (m_root.startsWith(home + QLatin1Char('/')))
        return QLatin1Char('~') + m_root.mid(home.size());
#endif
    return QDir::toNativeSeparators(m_root);
}

int Workspace::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_projects.size();
}

QVariant Workspace::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_projects.size())
        return {};
    const Project &p = m_projects.at(index.row());
    switch (role) {
    case NameRole: return p.name;
    case PathRole: return p.path;
    case ModifiedRole: return p.modified;
    default: return {};
    }
}

QHash<int, QByteArray> Workspace::roleNames() const
{
    return {{NameRole, "name"}, {PathRole, "path"}, {ModifiedRole, "modified"}};
}

int Workspace::currentIndex() const
{
    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects.at(i).path == m_currentPath)
            return i;
    }
    return -1;
}

void Workspace::setCurrentIndex(int row)
{
    if (row < 0 || row >= m_projects.size())
        return;
    setCurrentPath(m_projects.at(row).path);
}

QString Workspace::currentName() const
{
    return m_currentPath.isEmpty() ? QString() : QFileInfo(m_currentPath).fileName();
}

void Workspace::setCurrentPath(const QString &path, bool force)
{
    if (path == m_currentPath && !force)
        return;
    m_currentPath = path;
    QSettings().setValue(QStringLiteral("workspace/lastProject"), currentName());
    emit currentChanged();
}

void Workspace::setRootPath(const QUrl &folder)
{
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (path.isEmpty())
        return;
    QDir().mkpath(path);
    m_root = QDir(path).absolutePath();
    QSettings().setValue(QStringLiteral("workspace/root"), m_root);
    scan();
    ensureProject();
    emit rootPathChanged();
    setCurrentPath(m_projects.first().path, true);
}

void Workspace::scan()
{
    beginResetModel();
    m_projects.clear();
    const QFileInfoList dirs = QDir(m_root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                          QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : dirs) {
        if (fi.fileName().startsWith(QLatin1Char('.')))
            continue;
        QFileInfo canvas(QDir(fi.absoluteFilePath()).filePath(QStringLiteral("canvas.json")));
        m_projects.append({fi.fileName(), fi.absoluteFilePath(),
                           canvas.exists() ? canvas.lastModified() : fi.lastModified()});
    }
    endResetModel();
}

void Workspace::refresh()
{
    const QString current = m_currentPath;
    scan();
    ensureProject();
    if (currentIndex() < 0)
        setCurrentPath(m_projects.first().path);
    else if (current != m_currentPath)
        emit currentChanged();
}

void Workspace::ensureProject()
{
    if (!m_projects.isEmpty())
        return;
    const QString path = QDir(m_root).filePath(QStringLiteral("Welcome"));
    QDir().mkpath(path);
    writeWelcome(path);
    scan();
}

QString Workspace::sanitize(const QString &name) const
{
    QString clean = name.trimmed();
    clean.replace(QLatin1Char('/'), QLatin1Char('-'));
    clean.replace(QLatin1Char('\\'), QLatin1Char('-'));
    while (clean.startsWith(QLatin1Char('.')))
        clean.remove(0, 1);
    return clean.trimmed();
}

int Workspace::createProject(const QString &name)
{
    QString base = sanitize(name);
    if (base.isEmpty())
        base = QStringLiteral("Untitled");
    QDir root(m_root);
    QString candidate = base;
    for (int n = 2; root.exists(candidate); ++n)
        candidate = QStringLiteral("%1 %2").arg(base).arg(n);
    if (!root.mkpath(candidate))
        return -1;
    const QString path = root.filePath(candidate);
    QFile canvas(QDir(path).filePath(QStringLiteral("canvas.json")));
    if (canvas.open(QIODevice::WriteOnly))
        canvas.write(R"({ "version": 1, "items": [] })");
    scan();
    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects.at(i).path == path)
            return i;
    }
    return -1;
}

bool Workspace::renameProject(int row, const QString &name)
{
    if (row < 0 || row >= m_projects.size())
        return false;
    const QString clean = sanitize(name);
    if (clean.isEmpty())
        return false;
    const QString from = m_projects.at(row).path;
    const QString to = QDir(m_root).filePath(clean);
    if (from == to)
        return true;
    if (QFileInfo::exists(to) || !QDir().rename(from, to))
        return false;
    const bool wasCurrent = from == m_currentPath;
    scan();
    if (wasCurrent)
        setCurrentPath(to);
    return true;
}

bool Workspace::removeProject(int row)
{
    if (row < 0 || row >= m_projects.size())
        return false;
    const QString path = m_projects.at(row).path;
    if (!QFile::moveToTrash(path))
        return false;
    scan();
    ensureProject();
    if (path == m_currentPath)
        setCurrentPath(m_projects.at(std::min<qsizetype>(row, m_projects.size() - 1)).path);
    return true;
}

int Workspace::indexOfPath(const QString &path) const
{
    const QString wanted = QDir(path).absolutePath();
    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects.at(i).path == wanted)
            return i;
    }
    return -1;
}

QUrl Workspace::projectUrl(int row) const
{
    if (row < 0 || row >= m_projects.size())
        return {};
    return QUrl::fromLocalFile(m_projects.at(row).path);
}

void Workspace::writeWelcome(const QString &projectPath) const
{
    const QDir dir(projectPath);
    dir.mkpath(QStringLiteral("notes"));
    struct Seed {
        const char *id;
        const char *text;
        double x, y, w, h;
        const char *color;
    };
    const Seed seeds[] = {
        {"welcome", kWelcome, -400, -260, 400, 280, ""},
        {"media", kMedia, 40, -260, 400, 360, "blue"},
        {"links", kLinks, -400, 60, 400, 300, "green"},
        {"shortcuts", kShortcuts, 40, 140, 400, 380, "yellow"},
    };
    QJsonArray items;
    int z = 1;
    for (const Seed &s : seeds) {
        QFile md(dir.filePath(QStringLiteral("notes/%1.md").arg(QLatin1String(s.id))));
        if (md.open(QIODevice::WriteOnly))
            md.write(s.text);
        QJsonObject o{
            {QStringLiteral("id"), QLatin1String(s.id)},
            {QStringLiteral("type"), QStringLiteral("note")},
            {QStringLiteral("x"), s.x},
            {QStringLiteral("y"), s.y},
            {QStringLiteral("w"), s.w},
            {QStringLiteral("h"), s.h},
            {QStringLiteral("z"), z++},
            {QStringLiteral("file"), QStringLiteral("notes/%1.md").arg(QLatin1String(s.id))},
        };
        if (*s.color)
            o.insert(QStringLiteral("color"), QLatin1String(s.color));
        items.append(o);
    }
    QFile canvas(dir.filePath(QStringLiteral("canvas.json")));
    if (canvas.open(QIODevice::WriteOnly))
        canvas.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("items"), items}})
                         .toJson(QJsonDocument::Indented));
}
