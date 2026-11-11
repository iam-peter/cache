#include "searchmodel.h"

#include "boardmodel.h"
#include "workspace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

constexpr int kMaxResults = 500;

} // namespace

SearchModel::SearchModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Typing and edits to the open project re-run the search once things
    // settle, not on every keystroke
    m_timer.setSingleShot(true);
    m_timer.setInterval(150);
    connect(&m_timer, &QTimer::timeout, this, &SearchModel::run);
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_results.size();
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_results.size())
        return {};
    const Result &r = m_results.at(index.row());
    switch (role) {
    case ProjectNameRole: return r.projectName;
    case ProjectPathRole: return r.projectPath;
    case NoteIdRole: return r.noteId;
    case TitleRole: return r.title;
    case SnippetRole: return r.snippet;
    case HitsRole: return r.hits;
    case CurrentProjectRole: return r.current;
    default: return {};
    }
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {
        {ProjectNameRole, "projectName"},
        {ProjectPathRole, "projectPath"},
        {NoteIdRole, "noteId"},
        {TitleRole, "title"},
        {SnippetRole, "snippet"},
        {HitsRole, "hits"},
        {CurrentProjectRole, "currentProject"},
    };
}

int SearchModel::projectCount() const
{
    QSet<QString> projects;
    for (const Result &r : m_results)
        projects.insert(r.projectPath);
    return projects.size();
}

void SearchModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    const QStringList terms = parseTerms(query);
    if (terms != m_terms) {
        m_terms = terms;
        m_timer.start();
    }
    emit queryChanged();
}

void SearchModel::refresh()
{
    if (active())
        m_timer.start();
}

QStringList SearchModel::parseTerms(const QString &query)
{
    static const QRegularExpression token(QStringLiteral(R"("([^"]*)\"?|(\S+))"));
    QStringList terms;
    for (auto it = token.globalMatch(query); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString term = (m.hasCaptured(1) ? m.captured(1) : m.captured(2)).trimmed();
        if (!term.isEmpty() && !terms.contains(term, Qt::CaseInsensitive))
            terms << term;
    }
    return terms;
}

QString SearchModel::plainText(const QString &markdown)
{
    static const QRegularExpression image(QStringLiteral(R"(!\[([^\]]*)\]\([^)]*\))"));
    static const QRegularExpression link(QStringLiteral(R"(\[([^\]]*)\]\([^)]*\))"));
    static const QRegularExpression marker(QStringLiteral(R"(^\s{0,3}(#{1,6}|>|[-*+]|\d+\.|\|)\s*)"),
                                           QRegularExpression::MultilineOption);
    static const QRegularExpression emphasis(QStringLiteral(R"(\*\*|__|`+|~~|<[^>]+>|\|\s*-+\s*)"));
    static const QRegularExpression space(QStringLiteral(R"(\s+)"));
    QString text = markdown;
    text.replace(image, QStringLiteral("\\1"));
    text.replace(link, QStringLiteral("\\1"));
    text.replace(marker, QString());
    text.replace(emphasis, QStringLiteral(" "));
    text.replace(QLatin1Char('|'), QLatin1Char(' '));
    text.replace(space, QStringLiteral(" "));
    return text.trimmed();
}

QString SearchModel::snippet(const QString &plain, const QStringList &terms)
{
    QStringList escaped;
    for (const QString &term : terms)
        escaped << QRegularExpression::escape(term);
    const QRegularExpression any(escaped.join(QLatin1Char('|')), QRegularExpression::CaseInsensitiveOption);

    const qsizetype first = std::max<qsizetype>(0, plain.indexOf(any));
    qsizetype start = std::max<qsizetype>(0, first - 50);
    if (start > 0) { // begin at a word
        const qsizetype space = plain.indexOf(QLatin1Char(' '), start);
        if (space >= 0 && space < first)
            start = space + 1;
    }
    qsizetype end = std::min<qsizetype>(plain.size(), start + 170);
    if (end < plain.size()) {
        const qsizetype space = plain.lastIndexOf(QLatin1Char(' '), end);
        if (space > first)
            end = space;
    }
    const QString window = plain.mid(start, end - start);

    QString html = start > 0 ? QStringLiteral("…") : QString();
    qsizetype at = 0;
    for (auto it = any.globalMatch(window); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        html += window.mid(at, m.capturedStart() - at).toHtmlEscaped();
        html += QStringLiteral("<b>") + m.captured().toHtmlEscaped() + QStringLiteral("</b>");
        at = m.capturedEnd();
    }
    html += window.mid(at).toHtmlEscaped();
    if (end < plain.size())
        html += QStringLiteral("…");
    return html;
}

bool SearchModel::match(const QString &text, int *hits) const
{
    int total = 0;
    for (const QString &term : m_terms) {
        const int n = int(text.count(term, Qt::CaseInsensitive));
        if (n == 0)
            return false;
        total += n;
    }
    *hits = total;
    return true;
}

// The open project and the project list come from the app's own
// singletons; edits to either re-run the search.
void SearchModel::connectSources()
{
    if (m_board && m_workspace)
        return;
    QQmlEngine *engine = qmlEngine(this);
    if (!engine)
        return;
    m_board = engine->singletonInstance<BoardModel *>("NoteApp", "Board");
    m_workspace = engine->singletonInstance<Workspace *>("NoteApp", "Workspace");
    if (m_board) {
        connect(m_board, &QAbstractItemModel::dataChanged, this, &SearchModel::refresh);
        connect(m_board, &QAbstractItemModel::rowsInserted, this, &SearchModel::refresh);
        connect(m_board, &QAbstractItemModel::rowsRemoved, this, &SearchModel::refresh);
        connect(m_board, &QAbstractItemModel::modelReset, this, &SearchModel::refresh);
    }
    if (m_workspace) {
        connect(m_workspace, &Workspace::rootPathChanged, this, &SearchModel::refresh);
        connect(m_workspace, &QAbstractItemModel::modelReset, this, &SearchModel::refresh);
    }
}

void SearchModel::run()
{
    connectSources();
    QList<Result> results;

    if (active() && m_workspace) {
        const QString open = m_board ? QDir(m_board->projectPath()).absolutePath() : QString();
        const auto consider = [&](const QString &projectName, const QString &projectPath, const QString &id,
                                  const QString &text, bool current) {
            int hits = 0;
            if (!match(text, &hits))
                return;
            const QString title = BoardModel::titleForText(text);
            for (const QString &term : std::as_const(m_terms)) // title matches first
                hits += title.contains(term, Qt::CaseInsensitive) ? 1000 : 0;
            QString plain = plainText(text);
            const QString plainTitle = plainText(title);
            if (plain.startsWith(plainTitle))
                plain = plain.mid(plainTitle.size()).trimmed();
            results.append({projectName, projectPath, id, title, snippet(plain, m_terms), hits, current});
        };

        QSet<QString> seen;
        const QFileInfoList projects = QDir(m_workspace->rootPath())
                                           .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &project : projects) {
            if (project.fileName().startsWith(QLatin1Char('.')))
                continue;
            const QString path = project.absoluteFilePath();
            if (m_board && path == open) {
                for (const BoardItem &it : m_board->items()) {
                    if (it.type == QLatin1String("note"))
                        consider(project.fileName(), path, it.id, it.text, true);
                }
                continue;
            }
            const QFileInfoList notes = QDir(QDir(path).filePath(QStringLiteral("notes")))
                                            .entryInfoList({QStringLiteral("*.md")}, QDir::Files);
            for (const QFileInfo &note : notes) {
                const QString file = note.absoluteFilePath();
                seen.insert(file);
                CachedNote &cached = m_cache[file];
                if (cached.modified != note.lastModified()) {
                    QFile f(file);
                    cached.text = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
                    cached.modified = note.lastModified();
                }
                consider(project.fileName(), path, note.completeBaseName(), cached.text, false);
            }
        }
        // Forget notes that are gone
        for (auto it = m_cache.begin(); it != m_cache.end();)
            it = seen.contains(it.key()) ? std::next(it) : m_cache.erase(it);

        std::stable_sort(results.begin(), results.end(), [](const Result &a, const Result &b) {
            if (a.current != b.current)
                return a.current;
            if (a.projectPath != b.projectPath)
                return a.projectName.compare(b.projectName, Qt::CaseInsensitive) < 0;
            if (a.hits != b.hits)
                return a.hits > b.hits;
            return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
        });
        if (results.size() > kMaxResults)
            results.resize(kMaxResults);
    }

    beginResetModel();
    m_results = std::move(results);
    endResetModel();
    emit resultsChanged();
}
