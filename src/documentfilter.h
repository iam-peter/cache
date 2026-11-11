#pragma once

#include <QSortFilterProxyModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// The open canvas for the sidebar: its notes chapter by chapter (each
// section's title, then the notes inside it), in reading order, without
// those in folded branches. Optionally filtered by a search string.
class DocumentFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

public:
    explicit DocumentFilterModel(QObject *parent = nullptr);

    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text);

signals:
    void filterTextChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_filterText;
    QTimer m_resort; // the order depends on roles Qt doesn't re-sort for
};
