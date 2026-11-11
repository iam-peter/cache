#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// The folder that holds all projects. Every subfolder is one project.
class Workspace : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootPathChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, PathRole, ModifiedRole };

    explicit Workspace(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString rootPath() const { return m_root; }
    int currentIndex() const;
    void setCurrentIndex(int row);
    QString currentPath() const { return m_currentPath; }
    QString currentName() const;

    Q_INVOKABLE void setRootPath(const QUrl &folder);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE int createProject(const QString &name);
    Q_INVOKABLE bool renameProject(int row, const QString &name);
    Q_INVOKABLE bool removeProject(int row);
    Q_INVOKABLE QUrl projectUrl(int row) const;
    Q_INVOKABLE int indexOfPath(const QString &path) const;

signals:
    void rootPathChanged();
    void currentChanged();

private:
    struct Project {
        QString name;
        QString path;
        QDateTime modified;
    };

    void scan();
    void ensureProject();
    void setCurrentPath(const QString &path, bool force = false);
    QString sanitize(const QString &name) const;
    void writeWelcome(const QString &projectPath) const;

    QString m_root;
    QString m_currentPath;
    QList<Project> m_projects;
};
