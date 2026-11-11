#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Typo-tolerant matching for the search. Free of the rest of the app so the
// tests can use it on its own.
namespace Fuzzy {

// Lower case without accents: "Café" -> "cafe", "Straße" -> "strasse"
QString fold(const QString &text);

// Edit distance where swapping two neighbouring letters counts as one
// edit (optimal string alignment). Stops early: anything above `max`
// comes back as max + 1.
int distance(const QString &a, const QString &b, int max);

// How many typos a query word of this length may have: none for short
// words, which would otherwise match almost anything
int allowedTypos(qsizetype length);

// A note's text prepared for matching: folded, and its distinct words,
// each folded word with the forms it has in the text
struct Index {
    QString folded;
    QHash<QString, QStringList> words;
};
Index index(const QString &text);

enum class Match { None, Typo, Exact };

// Whether `term` (folded) is in the note. Exact: it occurs as it is,
// within or across words. Typo: a word of the note, or its beginning, is
// a few edits away. `forms` gets the words of the note that matched, as
// they are written there, for highlighting.
Match match(const QString &term, const Index &index, QStringList *forms = nullptr);

} // namespace Fuzzy
