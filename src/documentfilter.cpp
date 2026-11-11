#include "documentfilter.h"

#include "boardmodel.h"

#include <cmath>

DocumentFilterModel::DocumentFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    sort(0);
}

void DocumentFilterModel::setFilterText(const QString &text)
{
    if (m_filterText == text)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_filterText = text;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    m_filterText = text;
    invalidateFilter();
#endif
    emit filterTextChanged();
}

bool DocumentFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QString needle = m_filterText.trimmed();
    if (needle.isEmpty())
        return true;
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    return idx.data(BoardModel::SearchRole).toString().contains(needle, Qt::CaseInsensitive);
}

bool DocumentFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // Items whose tops are within one band count as the same row
    const int bandL = int(std::floor(left.data(BoardModel::PosYRole).toDouble() / 120.0));
    const int bandR = int(std::floor(right.data(BoardModel::PosYRole).toDouble() / 120.0));
    if (bandL != bandR)
        return bandL < bandR;
    return left.data(BoardModel::PosXRole).toDouble() < right.data(BoardModel::PosXRole).toDouble();
}
