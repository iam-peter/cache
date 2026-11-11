#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class BoardModel;
class Workspace;

// Full-text search over every note of every project in the workspace.
//
// A query is words and "quoted phrases"; a note matches when it contains
// all of them, ignoring case. The open project is searched as it is in
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
    Q_PROPERTY(bool active READ active NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY resultsChanged)
    Q_PROPERTY(int projectCount READ projectCount NOTIFY resultsChanged)

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
    bool active() const { return !m_terms.isEmpty(); }
    int count() const { return m_results.size(); }
    int projectCount() const;

    Q_INVOKABLE void refresh();

    static QStringList parseTerms(const QString &query);
    // Markdown reduced to its words, for snippets
    static QString plainText(const QString &markdown);
    // Up to ~160 characters around the first match, HTML-escaped, the
    // matches in <b>
    static QString snippet(const QString &plain, const QStringList &terms);

signals:
    void queryChanged();
    void resultsChanged();

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
    };

    void connectSources();
    void run();
    bool match(const QString &text, int *hits) const;

    QString m_query;
    QStringList m_terms;
    QList<Result> m_results;
    QHash<QString, CachedNote> m_cache; // .md path -> contents
    QTimer m_timer;
    BoardModel *m_board = nullptr;
    Workspace *m_workspace = nullptr;
};
