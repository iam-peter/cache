#include "fuzzy.h"

#include <QRegularExpression>
#include <QVarLengthArray>

#include <algorithm>

namespace Fuzzy {

QString fold(const QString &text)
{
    // Compatibility decomposition splits "é" into "e" and a combining
    // accent, which is then dropped. QString's case folding keeps "ß", so
    // that one is spelled out.
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            out += c;
    }
    out = out.toCaseFolded();
    out.replace(QChar(0x00DF), QStringLiteral("ss"));
    return out;
}

int distance(const QString &a, const QString &b, int max)
{
    const qsizetype n = a.size();
    const qsizetype m = b.size();
    if (std::abs(n - m) > max)
        return max + 1;
    if (n == 0 || m == 0)
        return int(std::max(n, m));

    QVarLengthArray<int, 64> before(m + 1), previous(m + 1), current(m + 1);
    for (qsizetype j = 0; j <= m; ++j)
        previous[j] = int(j);
    for (qsizetype i = 1; i <= n; ++i) {
        current[0] = int(i);
        int best = current[0];
        for (qsizetype j = 1; j <= m; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            int d = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
            if (i > 1 && j > 1 && a.at(i - 1) == b.at(j - 2) && a.at(i - 2) == b.at(j - 1))
                d = std::min(d, before[j - 2] + 1); // a swap of neighbours
            current[j] = d;
            best = std::min(best, d);
        }
        if (best > max)
            return max + 1;
        std::swap(before, previous);
        std::swap(previous, current);
    }
    return std::min(previous[m], max + 1);
}

int allowedTypos(qsizetype length)
{
    return length <= 3 ? 0 : length <= 6 ? 1 : 2;
}

Index index(const QString &text)
{
    static const QRegularExpression word(QStringLiteral(R"([\p{L}\p{N}]+)"),
                                         QRegularExpression::UseUnicodePropertiesOption);
    Index result;
    result.folded = fold(text);
    for (auto it = word.globalMatch(text); it.hasNext();) {
        const QString form = it.next().captured();
        QStringList &forms = result.words[fold(form)];
        if (!forms.contains(form))
            forms << form;
    }
    return result;
}

Match match(const QString &term, const Index &index, QStringList *forms)
{
    if (term.isEmpty())
        return Match::None;

    // Exact: the words it occurs in are what to highlight; a phrase or a
    // piece across words highlights as typed
    if (index.folded.contains(term)) {
        if (forms) {
            const qsizetype before = forms->size();
            for (auto it = index.words.cbegin(); it != index.words.cend(); ++it) {
                if (it.key().contains(term))
                    *forms += it.value();
            }
            if (forms->size() == before)
                *forms << term;
        }
        return Match::Exact;
    }

    // Typos only for single words long enough to have them
    const int max = allowedTypos(term.size());
    if (max == 0 || term.contains(QLatin1Char(' ')))
        return Match::None;

    bool found = false;
    for (auto it = index.words.cbegin(); it != index.words.cend(); ++it) {
        const QString &word = it.key();
        if (word.size() < 3)
            continue;
        bool close = distance(term, word, max) <= max;
        // The start of a longer word, as while still typing it: "rolbac"
        // finds "rollback". Only for longer terms; "note" would otherwise
        // find "nothing".
        for (qsizetype len = term.size(); !close && term.size() >= 5 && len <= term.size() + 1; ++len) {
            if (len < word.size())
                close = distance(term, word.left(len), max) <= max;
        }
        if (!close)
            continue;
        found = true;
        if (!forms)
            break;
        *forms += it.value();
    }
    return found ? Match::Typo : Match::None;
}

} // namespace Fuzzy
