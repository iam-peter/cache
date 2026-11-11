#include "documentfilter.h"

#include "boardmodel.h"

#include <algorithm>


DocumentFilterModel::DocumentFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    sort(0);
    m_resort.setSingleShot(true);
    m_resort.setInterval(120);
    connect(&m_resort, &QTimer::timeout, this, [this] { invalidate(); });
    connect(this, &QAbstractProxyModel::sourceModelChanged, this, [this] {
        if (!sourceModel())
            return;
        connect(sourceModel(), &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                    static const QList<int> watched{BoardModel::SectionOrderRole, BoardModel::ReadingOrderRole,
                                                    BoardModel::HiddenRole, BoardModel::TitleRole, BoardModel::TextRole};
                    if (roles.isEmpty() || std::any_of(roles.cbegin(), roles.cend(),
                                                       [](int r) { return watched.contains(r); }))
                        m_resort.start();
                });
    });
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
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    // Notes, and sections as their chapters; nothing in a folded branch
    const QString type = idx.data(BoardModel::TypeRole).toString();
    if (type != QLatin1String("note") && type != QLatin1String("section"))
        return false;
    if (idx.data(BoardModel::HiddenRole).toBool())
        return false;
    const QString needle = m_filterText.trimmed();
    if (needle.isEmpty())
        return true;
    return idx.data(BoardModel::SearchRole).toString().contains(needle, Qt::CaseInsensitive);
}

bool DocumentFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // Section by section (notes outside any first), each section's title
    // before its notes, then reading order
    const int groupL = left.data(BoardModel::SectionOrderRole).toInt();
    const int groupR = right.data(BoardModel::SectionOrderRole).toInt();
    if (groupL != groupR)
        return groupL < groupR;
    const bool sectionL = left.data(BoardModel::TypeRole).toString() == QLatin1String("section");
    const bool sectionR = right.data(BoardModel::TypeRole).toString() == QLatin1String("section");
    if (sectionL != sectionR)
        return sectionL;
    return left.data(BoardModel::ReadingOrderRole).toInt() < right.data(BoardModel::ReadingOrderRole).toInt();
}
