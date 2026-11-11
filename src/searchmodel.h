#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "fuzzy.h"

class BoardModel;
class Workspace;

// Full-text search over every note of every project in the workspace.
//
// A query is words and "quoted phrases"; a note matches when it contains
// all of them, ignoring case and accents. Words also match despite typos
// and while still being typed (see Fuzzy::match); exact matches rank above
// those. The open project is searched as it is in
// memory, the others from their notes/*.md files, which are cached by
// modification time so repeated searches only read what changed.
class SearchModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Search)
    QML_SINGLETON

    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // The query's words and phrases, for highlighting matches
    Q_PROPERTY(QStringList terms READ terms NOTIFY queryChanged)
    // The words of the notes that matched, as written there, for
    // highlighting them in the notes: with typos, not the same as terms
    Q_PROPERTY(QStringList highlightWords READ highlightWords NOTIFY resultsChanged)
    Q_PROPERTY(bool active READ active NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY resultsChanged)
    Q_PROPERTY(int projectCount READ projectCount NOTIFY resultsChanged)
    // Goes up whenever a note anywhere may have changed: bindings that show
    // backlinks or tasks read it to stay current
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum Roles {
        ProjectNameRole = Qt::UserRole + 1,
        ProjectPathRole,
        NoteIdRole,
        TitleRole,
        SnippetRole,
        HitsRole,
        CurrentProjectRole,
    };

    explicit SearchModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QStringList terms() const { return m_terms; }
    QStringList highlightWords() const { return m_highlight; }
    bool active() const { return !m_terms.isEmpty(); }
    int count() const { return m_results.size(); }
    int projectCount() const;

    int revision() const { return m_revision; }

    Q_INVOKABLE void refresh();

    // The note a [[link]] names: "Title" in the open project first, then
    // anywhere; "Project/Title" in that project. Returns found, projectPath,
    // projectName, noteId, title, text, current.
    Q_INVOKABLE QVariantMap findNote(const QString &target);
    // Notes whose title starts with or contains `prefix`, for completing
    // [[links]]: title, projectName, projectPath, current, insert (what
    // goes between the brackets)
    Q_INVOKABLE QVariantList suggestNotes(const QString &prefix, int limit = 8);
    // Notes that link to this one: projectPath, projectName, noteId, title
    Q_INVOKABLE QVariantList backlinks(const QString &projectPath, const QString &noteId);
    Q_INVOKABLE int backlinkCount(const QString &projectPath, const QString &noteId);
    // Every unticked task in every project: projectName, projectPath,
    // noteId, noteTitle, task (its number in the note), text, current
    Q_INVOKABLE QVariantList openTasks();
    Q_INVOKABLE void toggleTask(const QString &projectPath, const QString &noteId, int task);

    static QStringList parseTerms(const QString &query);
    // Markdown reduced to its words, for snippets
    static QString plainText(const QString &markdown);
    // Up to ~170 characters around the first of `words`, HTML-escaped,
    // each of them in <b>
    static QString snippet(const QString &plain, const QStringList &words);

signals:
    void queryChanged();
    void resultsChanged();
    void revisionChanged();

private:
    struct Result {
        QString projectName;
        QString projectPath;
        QString noteId;
        QString title;
        QString snippet;
        int hits = 0;
        bool current = false;
    };
    struct CachedNote {
        QDateTime modified;
        QString text;
        QString title;
        Fuzzy::Index index;
    };
    // A note of the index
    struct Entry {
        QString projectName;
        QString projectPath;
        QString id;
        bool current = false;
        const CachedNote *note = nullptr;
    };

    void connectSources();
    void run();
    void touch();
    const QList<Entry> &index();
    int resolve(const QString &target, const QString &fromProject); // index of the Entry, or -1
    // Whether the note has every term; its score ranks the results and
    // `words` gets what to highlight
    bool match(const QString &title, const Fuzzy::Index &index, int *score, QStringList *words) const;

    QString m_query;
    QStringList m_terms;
    QStringList m_folded; // the terms, folded
    QStringList m_highlight;
    QList<Result> m_results;
    QHash<QString, CachedNote> m_cache; // .md path -> contents
    QHash<QString, CachedNote> m_openCache; // open project: note id -> contents
    QTimer m_timer;
    QTimer m_touchTimer;
    QList<Entry> m_entries;
    QHash<QString, QList<int>> m_backlinks; // projectPath \n id -> entries linking to it
    bool m_indexDirty = true;
    qint64 m_indexedAt = 0;
    int m_revision = 0;
    BoardModel *m_board = nullptr;
    Workspace *m_workspace = nullptr;
};
