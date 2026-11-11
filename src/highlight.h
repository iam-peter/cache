#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

// A small syntax highlighter for the code blocks of notes. It knows the
// shape of common languages (C-like ones, Python, shell, SQL, JSON, YAML,
// CMake, XML/HTML, CSS) well enough to colour keywords, types, strings,
// numbers, comments and calls; it does not parse.
namespace Highlight {

enum class Kind { Keyword, Type, String, Number, Comment, Preprocessor, Function };

struct Span {
    qsizetype start = 0;
    qsizetype length = 0;
    Kind kind = Kind::Keyword;
};

// The language a fence names, normalised ("c++" and "hpp" are "cpp"), or
// a guess from the code when it names none
QString language(const QString &fenceLanguage, const QString &code);

QList<Span> spans(const QString &code, const QString &language);

// The code as HTML for inside a <pre>: escaped, the spans coloured with
// colors.keyword, .type, .string, .number, .comment, .preprocessor and
// .function (any missing one stays the text colour)
QString toHtml(const QString &code, const QString &language, const QVariantMap &colors);

} // namespace Highlight
