#include "searchmodel.h"

#include "boardmodel.h"
#include "markdown.h"
#include "workspace.h"

#include <QDateTime>
#include <QDir>
#include <QSaveFile>
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
    m_touchTimer.setSingleShot(true);
    m_touchTimer.setInterval(300);
    connect(&m_touchTimer, &QTimer::timeout, this, [this] {
        m_indexDirty = true;
        ++m_revision;
        emit revisionChanged();
    });
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
        m_folded.clear();
        for (const QString &term : terms)
            m_folded << Fuzzy::fold(term);
        m_timer.start();
    }
    emit queryChanged();
}

void SearchModel::refresh()
{
    if (active())
        m_timer.start();
}

// Something changed: the index is rebuilt on its next use, and bindings
// that read revision ask again
void SearchModel::touch()
{
    m_indexDirty = true;
    if (active())
        m_timer.start();
    m_touchTimer.start();
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

QString SearchModel::snippet(const QString &plain, const QStringList &words)
{
    // Longest first, so "deployment" wins over "deploy" where both match
    QStringList sorted = words;
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    QStringList escaped;
    for (const QString &word : std::as_const(sorted))
        escaped << QRegularExpression::escape(word);
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

bool SearchModel::match(const QString &title, const Fuzzy::Index &index, int *score, QStringList *words) const
{
    const Fuzzy::Index titleIndex = Fuzzy::index(title);
    int total = 0;
    for (const QString &term : m_folded) {
        const Fuzzy::Match found = Fuzzy::match(term, index, words);
        if (found == Fuzzy::Match::None)
            return false;
        // Exact beats typo; each occurrence and a match in the title count
        // on top
        total += found == Fuzzy::Match::Exact ? 100 + int(index.folded.count(term)) : 40;
        const Fuzzy::Match inTitle = Fuzzy::match(term, titleIndex);
        total += inTitle == Fuzzy::Match::Exact ? 1000 : inTitle == Fuzzy::Match::Typo ? 500 : 0;
    }
    *score = total;
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
        connect(m_board, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                    // Only text matters; moving notes around doesn't
                    if (roles.isEmpty() || roles.contains(BoardModel::TextRole))
                        touch();
                });
        connect(m_board, &QAbstractItemModel::rowsInserted, this, &SearchModel::touch);
        connect(m_board, &QAbstractItemModel::rowsRemoved, this, &SearchModel::touch);
        connect(m_board, &QAbstractItemModel::modelReset, this, &SearchModel::touch);
    }
    if (m_workspace) {
        connect(m_workspace, &Workspace::rootPathChanged, this, &SearchModel::touch);
        connect(m_workspace, &QAbstractItemModel::modelReset, this, &SearchModel::touch);
    }
}

// Every note of every project, the open project's from memory. Rebuilt
// when something changed or a few seconds passed (files elsewhere may have
// changed); unchanged files come from the cache.
const QList<SearchModel::Entry> &SearchModel::index()
{
    connectSources();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_indexDirty && now - m_indexedAt < 3000)
        return m_entries;
    m_indexDirty = false;
    m_indexedAt = now;
    m_entries.clear();
    m_backlinks.clear();
    if (!m_workspace)
        return m_entries;

    const QString open = m_board ? QDir(m_board->projectPath()).absolutePath() : QString();
    QSet<QString> seen;
    QSet<QString> live;
    const QFileInfoList projects = QDir(m_workspace->rootPath())
                                       .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &project : projects) {
        if (project.fileName().startsWith(QLatin1Char('.')))
            continue;
        const QString path = project.absoluteFilePath();
        if (m_board && path == open) {
            for (const BoardItem &it : m_board->items()) {
                if (it.type != QLatin1String("note"))
                    continue;
                live.insert(it.id);
                CachedNote &cached = m_openCache[it.id];
                if (cached.text != it.text || cached.title.isEmpty()) {
                    cached.text = it.text;
                    cached.title = BoardModel::titleForText(it.text);
                    cached.index = Fuzzy::index(it.text);
                }
                m_entries.append({project.fileName(), path, it.id, true, &cached});
            }
            continue;
        }
        const QFileInfoList notes = QDir(QDir(path).filePath(QStringLiteral("notes")))
                                        .entryInfoList({QStringLiteral("*.md")}, QDir::Files);
        for (const QFileInfo &note : notes) {
            const QString file = note.absoluteFilePath();
            seen.insert(file);
            CachedNote &cached = m_cache[file];
            if (cached.modified != note.lastModified() || cached.title.isEmpty()) {
                QFile f(file);
                cached.text = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
                cached.title = BoardModel::titleForText(cached.text);
                cached.index = Fuzzy::index(cached.text);
                cached.modified = note.lastModified();
            }
            m_entries.append({project.fileName(), path, note.completeBaseName(), false, &cached});
        }
    }
    // Forget notes that are gone. Entries point into the caches, so this
    // happens after the last insertion and before anything reads them.
    for (auto it = m_cache.begin(); it != m_cache.end();)
        it = seen.contains(it.key()) ? std::next(it) : m_cache.erase(it);
    for (auto it = m_openCache.begin(); it != m_openCache.end();)
        it = live.contains(it.key()) ? std::next(it) : m_openCache.erase(it);
    // QHash may have moved its nodes while growing: point the entries again
    for (Entry &e : m_entries) {
        e.note = e.current ? &m_openCache[e.id]
                           : &m_cache[QDir(QDir(e.projectPath).filePath(QStringLiteral("notes"))).filePath(e.id + QStringLiteral(".md"))];
    }

    // Who links to whom
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        if (!e.note->text.contains(QLatin1String("[[")))
            continue;
        QSet<int> targets;
        for (const Markdown::WikiLink &link : Markdown::wikiLinks(e.note->text)) {
            const int target = resolve(link.target, e.projectPath);
            if (target >= 0 && target != i && !targets.contains(target)) {
                targets.insert(target);
                const Entry &t = m_entries.at(target);
                m_backlinks[t.projectPath + QLatin1Char('\n') + t.id].append(i);
            }
        }
    }
    return m_entries;
}

int SearchModel::resolve(const QString &target, const QString &fromProject)
{
    QString project;
    QString title = target.trimmed();
    const qsizetype slash = title.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        const QString maybe = title.left(slash).trimmed();
        for (const Entry &e : std::as_const(m_entries)) {
            if (e.projectName.compare(maybe, Qt::CaseInsensitive) == 0) {
                project = e.projectPath;
                title = title.mid(slash + 1).trimmed();
                break;
            }
        }
    }
    int elsewhere = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        if (e.note->title.compare(title, Qt::CaseInsensitive) != 0)
            continue;
        if (!project.isEmpty()) {
            if (e.projectPath == project)
                return i;
            continue;
        }
        if (e.projectPath == fromProject)
            return i;
        if (elsewhere < 0)
            elsewhere = i;
    }
    return project.isEmpty() ? elsewhere : -1;
}

QVariantMap SearchModel::findNote(const QString &target)
{
    index();
    const QString open = m_board ? QDir(m_board->projectPath()).absolutePath() : QString();
    const int i = resolve(target, open);
    if (i < 0)
        return {{QStringLiteral("found"), false}, {QStringLiteral("title"), target}};
    const Entry &e = m_entries.at(i);
    return {
        {QStringLiteral("found"), true},
        {QStringLiteral("projectPath"), e.projectPath},
        {QStringLiteral("projectName"), e.projectName},
        {QStringLiteral("noteId"), e.id},
        {QStringLiteral("title"), e.note->title},
        {QStringLiteral("text"), e.note->text},
        {QStringLiteral("current"), e.current},
    };
}

QVariantList SearchModel::suggestNotes(const QString &prefix, int limit)
{
    index();
    const QString wanted = Fuzzy::fold(prefix.trimmed());
    struct Hit {
        int entry;
        int rank;
    };
    QList<Hit> hits;
    for (int i = 0; i < m_entries.size(); ++i) {
        const QString title = Fuzzy::fold(m_entries.at(i).note->title);
        int rank = -1;
        if (wanted.isEmpty())
            rank = 2;
        else if (title.startsWith(wanted))
            rank = 0;
        else if (title.contains(wanted))
            rank = 1;
        else if (wanted.size() >= 4 && Fuzzy::match(wanted, Fuzzy::index(m_entries.at(i).note->title)) != Fuzzy::Match::None)
            rank = 2;
        if (rank >= 0)
            hits.append({i, rank * 2 + (m_entries.at(i).current ? 0 : 1)});
    }
    std::stable_sort(hits.begin(), hits.end(), [&](const Hit &a, const Hit &b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return m_entries.at(a.entry).note->title.compare(m_entries.at(b.entry).note->title, Qt::CaseInsensitive) < 0;
    });
    QVariantList list;
    for (const Hit &hit : std::as_const(hits)) {
        if (list.size() >= limit)
            break;
        const Entry &e = m_entries.at(hit.entry);
        list.append(QVariantMap{
            {QStringLiteral("title"), e.note->title},
            {QStringLiteral("projectName"), e.projectName},
            {QStringLiteral("projectPath"), e.projectPath},
            {QStringLiteral("current"), e.current},
            {QStringLiteral("insert"), e.current ? e.note->title : e.projectName + QLatin1Char('/') + e.note->title},
        });
    }
    return list;
}

QVariantList SearchModel::backlinks(const QString &projectPath, const QString &noteId)
{
    index();
    QVariantList list;
    for (int i : m_backlinks.value(QDir(projectPath).absolutePath() + QLatin1Char('\n') + noteId)) {
        const Entry &e = m_entries.at(i);
        list.append(QVariantMap{
            {QStringLiteral("projectPath"), e.projectPath},
            {QStringLiteral("projectName"), e.projectName},
            {QStringLiteral("noteId"), e.id},
            {QStringLiteral("title"), e.note->title},
            {QStringLiteral("current"), e.current},
        });
    }
    return list;
}

int SearchModel::backlinkCount(const QString &projectPath, const QString &noteId)
{
    index();
    return int(m_backlinks.value(QDir(projectPath).absolutePath() + QLatin1Char('\n') + noteId).size());
}

QVariantList SearchModel::openTasks()
{
    index();
    QList<Entry> entries = m_entries;
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.current != b.current)
            return a.current;
        return a.projectName.compare(b.projectName, Qt::CaseInsensitive) < 0;
    });
    QVariantList list;
    for (const Entry &e : std::as_const(entries)) {
        const QList<Markdown::Task> tasks = Markdown::tasks(e.note->text);
        for (int n = 0; n < tasks.size(); ++n) {
            if (tasks.at(n).done)
                continue;
            list.append(QVariantMap{
                {QStringLiteral("projectName"), e.projectName},
                {QStringLiteral("projectPath"), e.projectPath},
                {QStringLiteral("noteId"), e.id},
                {QStringLiteral("noteTitle"), e.note->title},
                {QStringLiteral("task"), n},
                {QStringLiteral("text"), plainText(tasks.at(n).text)},
                {QStringLiteral("current"), e.current},
            });
        }
    }
    return list;
}

void SearchModel::toggleTask(const QString &projectPath, const QString &noteId, int task)
{
    connectSources();
    if (m_board && QDir(m_board->projectPath()).absolutePath() == QDir(projectPath).absolutePath()) {
        m_board->toggleTask(m_board->indexOf(noteId), task);
        return;
    }
    // A note of a project that isn't open: change its file
    const QString file = QDir(QDir(projectPath).filePath(QStringLiteral("notes"))).filePath(noteId + QStringLiteral(".md"));
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return;
    const QString before = QString::fromUtf8(in.readAll());
    in.close();
    const QString after = Markdown::toggleTask(before, task);
    if (after == before)
        return;
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(after.toUtf8());
        out.commit();
    }
    touch();
}

void SearchModel::run()
{
    QList<Result> results;
    if (active()) {
        QStringList highlight;
        for (const Entry &e : index()) {
            const CachedNote &note = *e.note;
            int score = 0;
            QStringList words;
            if (!match(note.title, note.index, &score, &words))
                continue;
            words.removeDuplicates();
            for (const QString &word : std::as_const(words)) {
                if (highlight.size() < 300 && !highlight.contains(word))
                    highlight << word;
            }
            QString plain = plainText(note.text);
            const QString plainTitle = plainText(note.title);
            if (plain.startsWith(plainTitle))
                plain = plain.mid(plainTitle.size()).trimmed();
            results.append({e.projectName, e.projectPath, e.id, note.title, snippet(plain, words), score, e.current});
        }
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
        m_highlight = highlight;
    } else {
        m_highlight.clear();
    }

    beginResetModel();
    m_results = std::move(results);
    endResetModel();
    emit resultsChanged();
}
