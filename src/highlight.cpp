#include "highlight.h"

#include <QColor>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace Highlight {

namespace {

struct Language {
    QStringList lineComments;
    QString blockStart;
    QString blockEnd;
    QString quotes;            // characters that open strings
    bool tripleQuotes = false; // Python's """
    bool caseInsensitive = false;
    bool hashPreprocessor = false; // #include
    bool decorators = false;       // @property
    bool variables = false;        // $VAR and ${VAR}
    bool markup = false;           // <tag attr="">
    bool capitalTypes = false;     // Foo is a type
    QSet<QString> keywords;
    QSet<QString> types;
};

QSet<QString> words(const char *list)
{
    QSet<QString> set;
    for (const QString &w : QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        set.insert(w);
    return set;
}

const QHash<QString, Language> &languages()
{
    static const QHash<QString, Language> all = [] {
        QHash<QString, Language> l;

        Language c;
        c.lineComments = {QStringLiteral("//")};
        c.blockStart = QStringLiteral("/*");
        c.blockEnd = QStringLiteral("*/");
        c.quotes = QStringLiteral("\"'");
        c.capitalTypes = true;

        Language cpp = c;
        cpp.hashPreprocessor = true;
        cpp.keywords = words("alignas alignof auto break case catch class const constexpr consteval constinit "
                             "const_cast continue co_await co_return co_yield decltype default delete do "
                             "dynamic_cast else enum explicit export extern false final for friend goto if "
                             "inline mutable namespace new noexcept nullptr operator override private "
                             "protected public reinterpret_cast requires return sizeof static static_assert "
                             "static_cast struct switch template this throw true try typedef typeid typename "
                             "union using virtual volatile while emit signals slots");
        cpp.types = words("bool char char8_t char16_t char32_t double float int long short signed unsigned "
                          "void wchar_t size_t qsizetype qreal uint8_t uint16_t uint32_t uint64_t int8_t "
                          "int16_t int32_t int64_t");
        l.insert(QStringLiteral("cpp"), cpp);

        Language js = c;
        js.quotes = QStringLiteral("\"'`");
        js.keywords = words("as async await break case catch class const continue debugger default delete do "
                            "else enum export extends false finally for from function get if implements import "
                            "in instanceof interface let new null of package private protected public readonly "
                            "required return set static super switch this throw true try type typeof undefined "
                            "var void while with yield property signal component pragma");
        js.types = words("any boolean number string symbol never unknown object int real bool var list url color "
                         "point rect size font date");
        l.insert(QStringLiteral("javascript"), js);

        Language java = c;
        java.decorators = true;
        java.keywords = words("abstract assert break case catch class const continue default do else enum "
                              "extends final finally for goto if implements import instanceof interface native "
                              "new package private protected public return static strictfp super switch "
                              "synchronized this throw throws transient try volatile while true false null var "
                              "fun val when object companion data sealed override open internal lateinit by in is "
                              "suspend");
        java.types = words("boolean byte char double float int long short void String Int Long Boolean Unit");
        l.insert(QStringLiteral("java"), java);

        Language cs = java;
        cs.hashPreprocessor = true;
        cs.keywords += words("namespace using struct readonly ref out params async await get set value yield "
                             "partial virtual sealed base lock checked unchecked event delegate operator");
        cs.types += words("string object decimal uint ulong ushort sbyte dynamic");
        l.insert(QStringLiteral("csharp"), cs);

        Language go = c;
        go.quotes = QStringLiteral("\"'`");
        go.keywords = words("break case chan const continue default defer else fallthrough for func go goto if "
                            "import interface map package range return select struct switch type var true false "
                            "nil iota");
        go.types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
                         "string uint uint8 uint16 uint32 uint64 uintptr any");
        l.insert(QStringLiteral("go"), go);

        Language rust = c;
        rust.quotes = QStringLiteral("\"");
        rust.hashPreprocessor = true; // #[derive]
        rust.keywords = words("as async await break const continue crate dyn else enum extern false fn for if impl "
                              "in let loop match mod move mut pub ref return self Self static struct super trait "
                              "true type unsafe use where while macro_rules");
        rust.types = words("bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize String Vec "
                           "Option Result Box");
        l.insert(QStringLiteral("rust"), rust);

        Language swift = c;
        swift.decorators = true;
        swift.keywords = words("as associatedtype break case catch class continue default defer deinit do else "
                               "enum extension fallthrough false fileprivate for func guard if import in init inout "
                               "internal is let nil open operator private protocol public repeat rethrows return "
                               "self Self static struct subscript super switch throw throws true try typealias var "
                               "where while async await");
        l.insert(QStringLiteral("swift"), swift);

        Language php = c;
        php.variables = true;
        php.lineComments << QStringLiteral("#");
        php.keywords = words("abstract and array as break callable case catch class clone const continue declare "
                             "default do echo else elseif empty enddeclare endfor endforeach endif endswitch "
                             "endwhile extends final finally fn for foreach function global goto if implements "
                             "include instanceof insteadof interface isset list match namespace new or print "
                             "private protected public readonly require return static switch throw trait try "
                             "unset use var while xor yield true false null");
        l.insert(QStringLiteral("php"), php);

        Language python;
        python.lineComments = {QStringLiteral("#")};
        python.quotes = QStringLiteral("\"'");
        python.tripleQuotes = true;
        python.decorators = true;
        python.capitalTypes = true;
        python.keywords = words("and as assert async await break class continue def del elif else except False "
                                "finally for from global if import in is lambda None nonlocal not or pass raise "
                                "return True try while with yield match case self");
        python.types = words("int float str bool bytes list dict set tuple object type complex frozenset");
        l.insert(QStringLiteral("python"), python);

        Language ruby = python;
        ruby.tripleQuotes = false;
        ruby.decorators = false;
        ruby.keywords = words("alias and begin break case class def defined do else elsif end ensure false for "
                              "if in module next nil not or redo rescue retry return self super then true undef "
                              "unless until when while yield require attr_accessor");
        l.insert(QStringLiteral("ruby"), ruby);

        Language shell;
        shell.lineComments = {QStringLiteral("#")};
        shell.quotes = QStringLiteral("\"'");
        shell.variables = true;
        shell.keywords = words("if then else elif fi for while until do done case esac in function return local "
                               "export readonly declare unset shift break continue exit source alias set trap "
                               "eval exec select time");
        shell.types = words("echo cd ls grep sed awk cat cp mv rm mkdir chmod chown find xargs sudo git cmake make "
                            "ninja curl wget tar ssh printf test read");
        l.insert(QStringLiteral("shell"), shell);

        Language sql;
        sql.lineComments = {QStringLiteral("--")};
        sql.blockStart = QStringLiteral("/*");
        sql.blockEnd = QStringLiteral("*/");
        sql.quotes = QStringLiteral("'\"");
        sql.caseInsensitive = true;
        sql.keywords = words("select from where and or not insert into values update set delete create table "
                             "drop alter add column index view join inner left right outer full on as group by "
                             "order having limit offset union all distinct case when then else end is null "
                             "primary key foreign references default exists in like between begin commit "
                             "rollback transaction with returning asc desc");
        sql.types = words("int integer bigint smallint text varchar char boolean bool date time timestamp "
                          "numeric decimal real float double serial uuid json jsonb blob");
        l.insert(QStringLiteral("sql"), sql);

        Language json;
        json.quotes = QStringLiteral("\"");
        json.keywords = words("true false null");
        l.insert(QStringLiteral("json"), json);

        Language yaml;
        yaml.lineComments = {QStringLiteral("#")};
        yaml.quotes = QStringLiteral("\"'");
        yaml.keywords = words("true false null yes no on off");
        l.insert(QStringLiteral("yaml"), yaml);

        Language cmake;
        cmake.lineComments = {QStringLiteral("#")};
        cmake.quotes = QStringLiteral("\"");
        cmake.variables = true;
        cmake.caseInsensitive = true;
        cmake.keywords = words("if elseif else endif foreach endforeach while endwhile function endfunction "
                               "macro endmacro return set unset option include add_subdirectory project "
                               "find_package target_link_libraries add_executable add_library "
                               "target_include_directories target_compile_definitions install message");
        cmake.types = words("PRIVATE PUBLIC INTERFACE REQUIRED COMPONENTS STATIC SHARED ON OFF TRUE FALSE");
        l.insert(QStringLiteral("cmake"), cmake);

        Language xml;
        xml.blockStart = QStringLiteral("<!--");
        xml.blockEnd = QStringLiteral("-->");
        xml.quotes = QStringLiteral("\"'");
        xml.markup = true;
        l.insert(QStringLiteral("xml"), xml);

        Language css;
        css.blockStart = QStringLiteral("/*");
        css.blockEnd = QStringLiteral("*/");
        css.quotes = QStringLiteral("\"'");
        css.keywords = words("important inherit initial unset none auto block inline flex grid absolute "
                             "relative fixed sticky solid dashed");
        l.insert(QStringLiteral("css"), css);

        Language plain;
        l.insert(QStringLiteral("text"), plain);
        return l;
    }();
    return all;
}

const QHash<QString, QString> &aliases()
{
    static const QHash<QString, QString> all = {
        {"c", "cpp"},      {"c++", "cpp"},     {"cc", "cpp"},       {"cxx", "cpp"},       {"h", "cpp"},
        {"hpp", "cpp"},    {"objc", "cpp"},    {"js", "javascript"}, {"jsx", "javascript"}, {"ts", "javascript"},
        {"tsx", "javascript"}, {"typescript", "javascript"}, {"qml", "javascript"}, {"mjs", "javascript"},
        {"node", "javascript"}, {"kotlin", "java"}, {"kt", "java"}, {"scala", "java"}, {"groovy", "java"},
        {"dart", "java"},  {"cs", "csharp"},   {"c#", "csharp"},    {"golang", "go"},     {"rs", "rust"},
        {"py", "python"},  {"python3", "python"}, {"rb", "ruby"},   {"sh", "shell"},      {"bash", "shell"},
        {"zsh", "shell"},  {"fish", "shell"},  {"console", "shell"}, {"terminal", "shell"}, {"ps1", "shell"},
        {"powershell", "shell"}, {"yml", "yaml"}, {"toml", "yaml"}, {"ini", "yaml"}, {"conf", "yaml"},
        {"jsonc", "json"}, {"json5", "json"},  {"html", "xml"},     {"htm", "xml"},       {"svg", "xml"},
        {"xhtml", "xml"},  {"plist", "xml"},   {"scss", "css"},     {"less", "css"},      {"postgres", "sql"},
        {"mysql", "sql"},  {"sqlite", "sql"},  {"plsql", "sql"},    {"txt", "text"},      {"plain", "text"},
    };
    return all;
}

bool isIdentStart(QChar c) { return c.isLetter() || c == QLatin1Char('_'); }
bool isIdent(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

} // namespace

QString language(const QString &fenceLanguage, const QString &code)
{
    QString name = fenceLanguage.trimmed().toLower();
    if (aliases().contains(name))
        name = aliases().value(name);
    if (languages().contains(name))
        return name;

    // A guess from typical lines
    const QString c = code.left(2000);
    const auto has = [&](const char *pattern) {
        return QRegularExpression(QString::fromLatin1(pattern), QRegularExpression::MultilineOption).match(c).hasMatch();
    };
    if (has(R"(^\s*#include\s*[<"]|std::|::\w+\()"))
        return QStringLiteral("cpp");
    if (has(R"(^\s*(def |import \w+$|from \w+ import|class \w+(\(.*\))?:$))"))
        return QStringLiteral("python");
    if (has(R"(^\s*(SELECT|INSERT|UPDATE|CREATE TABLE|DELETE FROM)\b)"))
        return QStringLiteral("sql");
    if (has(R"(^\s*[\[{]\s*$)") && has(R"(^\s*"[^"]+"\s*:)"))
        return QStringLiteral("json");
    if (has(R"(^\s*<[a-zA-Z!?])"))
        return QStringLiteral("xml");
    if (has(R"(^\s*(fn |let mut |use \w+::|impl ))"))
        return QStringLiteral("rust");
    if (has(R"(^\s*(func |package \w+$))"))
        return QStringLiteral("go");
    if (has(R"(^\s*(const|let|var|function|import|export)\s|=>)"))
        return QStringLiteral("javascript");
    if (has(R"(^\s*(public|private)\s+(static\s+)?(class|void))"))
        return QStringLiteral("java");
    if (has(R"(^\s*(\$ |#!/|sudo |git |cd |echo |export \w+=))"))
        return QStringLiteral("shell");
    if (has(R"(^\s*[\w-]+:\s)") && !has(R"([;{}])"))
        return QStringLiteral("yaml");
    return QStringLiteral("text");
}

QList<Span> spans(const QString &code, const QString &languageName)
{
    QList<Span> out;
    const Language &lang = languages().value(languageName, languages().value(QStringLiteral("text")));
    const qsizetype n = code.size();
    const auto startsAt = [&](qsizetype i, const QString &s) {
        return !s.isEmpty() && QStringView(code).mid(i).startsWith(s);
    };
    const auto keywordKind = [&](const QString &word) -> int {
        const QString key = lang.caseInsensitive ? word.toLower() : word;
        const auto in = [&](const QSet<QString> &set) {
            if (!lang.caseInsensitive)
                return set.contains(key);
            for (const QString &w : set) {
                if (w.compare(key, Qt::CaseInsensitive) == 0)
                    return true;
            }
            return false;
        };
        if (in(lang.keywords))
            return int(Kind::Keyword);
        if (in(lang.types))
            return int(Kind::Type);
        return -1;
    };

    bool lineStart = true;
    bool inTag = false;
    for (qsizetype i = 0; i < n;) {
        const QChar ch = code.at(i);
        if (ch == QLatin1Char('\n')) {
            lineStart = true;
            ++i;
            continue;
        }
        if (ch.isSpace()) {
            ++i;
            continue;
        }

        // Comments
        bool comment = false;
        for (const QString &marker : lang.lineComments) {
            if (startsAt(i, marker)) {
                qsizetype end = code.indexOf(QLatin1Char('\n'), i);
                if (end < 0)
                    end = n;
                out.append({i, end - i, Kind::Comment});
                i = end;
                comment = true;
                break;
            }
        }
        if (comment)
            continue;
        if (startsAt(i, lang.blockStart)) {
            qsizetype end = code.indexOf(lang.blockEnd, i + lang.blockStart.size());
            end = end < 0 ? n : end + lang.blockEnd.size();
            out.append({i, end - i, Kind::Comment});
            i = end;
            continue;
        }

        // Preprocessor lines and decorators
        if (lineStart && lang.hashPreprocessor && ch == QLatin1Char('#')) {
            qsizetype end = code.indexOf(QLatin1Char('\n'), i);
            if (end < 0)
                end = n;
            out.append({i, end - i, Kind::Preprocessor});
            i = end;
            continue;
        }
        lineStart = false;
        if (lang.decorators && ch == QLatin1Char('@') && i + 1 < n && isIdentStart(code.at(i + 1))) {
            qsizetype end = i + 1;
            while (end < n && (isIdent(code.at(end)) || code.at(end) == QLatin1Char('.')))
                ++end;
            out.append({i, end - i, Kind::Preprocessor});
            i = end;
            continue;
        }

        // Markup tags
        if (lang.markup && ch == QLatin1Char('<')) {
            qsizetype end = i + 1;
            if (end < n && (code.at(end) == QLatin1Char('/') || code.at(end) == QLatin1Char('?')
                            || code.at(end) == QLatin1Char('!')))
                ++end;
            while (end < n && (isIdent(code.at(end)) || code.at(end) == QLatin1Char('-') || code.at(end) == QLatin1Char(':')))
                ++end;
            out.append({i, end - i, Kind::Keyword});
            inTag = true;
            i = end;
            continue;
        }
        if (lang.markup && inTag && (ch == QLatin1Char('>') || startsAt(i, QStringLiteral("/>")))) {
            const qsizetype len = ch == QLatin1Char('>') ? 1 : 2;
            out.append({i, len, Kind::Keyword});
            inTag = false;
            i += len;
            continue;
        }

        // Strings
        if (lang.quotes.contains(ch)) {
            const QString triple(3, ch);
            if (lang.tripleQuotes && startsAt(i, triple)) {
                qsizetype end = code.indexOf(triple, i + 3);
                end = end < 0 ? n : end + 3;
                out.append({i, end - i, Kind::String});
                i = end;
                continue;
            }
            qsizetype end = i + 1;
            const bool multiline = ch == QLatin1Char('`');
            while (end < n && code.at(end) != ch && (multiline || code.at(end) != QLatin1Char('\n'))) {
                if (code.at(end) == QLatin1Char('\\') && ch != QLatin1Char('\''))
                    ++end;
                else if (code.at(end) == QLatin1Char('\\'))
                    ++end;
                ++end;
            }
            end = std::min(n, end + 1);
            // A JSON key, a YAML key: the string before a colon
            Kind kind = Kind::String;
            if (languageName == QLatin1String("json")) {
                qsizetype k = end;
                while (k < n && code.at(k) == QLatin1Char(' '))
                    ++k;
                if (k < n && code.at(k) == QLatin1Char(':'))
                    kind = Kind::Type;
            }
            out.append({i, end - i, kind});
            i = end;
            continue;
        }

        // Variables
        if (lang.variables && ch == QLatin1Char('$') && i + 1 < n) {
            qsizetype end = i + 1;
            if (code.at(end) == QLatin1Char('{')) {
                end = code.indexOf(QLatin1Char('}'), end);
                end = end < 0 ? n : end + 1;
            } else {
                while (end < n && isIdent(code.at(end)))
                    ++end;
            }
            if (end > i + 1) {
                out.append({i, end - i, Kind::Type});
                i = end;
                continue;
            }
        }

        // Numbers
        if (ch.isDigit() || (ch == QLatin1Char('.') && i + 1 < n && code.at(i + 1).isDigit())) {
            qsizetype end = i + 1;
            while (end < n && (code.at(end).isLetterOrNumber() || code.at(end) == QLatin1Char('.')
                               || code.at(end) == QLatin1Char('_') || code.at(end) == QLatin1Char('\'')))
                ++end;
            out.append({i, end - i, Kind::Number});
            i = end;
            continue;
        }

        // Words: keywords, types, calls; YAML keys
        if (isIdentStart(ch)) {
            qsizetype end = i + 1;
            while (end < n && (isIdent(code.at(end)) || (languageName == QLatin1String("yaml") && code.at(end) == QLatin1Char('-'))))
                ++end;
            const QString word = code.mid(i, end - i);
            const int kind = keywordKind(word);
            qsizetype next = end;
            while (next < n && code.at(next) == QLatin1Char(' '))
                ++next;
            if (languageName == QLatin1String("yaml") && next < n && code.at(next) == QLatin1Char(':'))
                out.append({i, end - i, Kind::Type});
            else if (lang.markup && inTag)
                out.append({i, end - i, Kind::Type}); // attribute
            else if (kind >= 0)
                out.append({i, end - i, Kind(kind)});
            else if (next < n && code.at(next) == QLatin1Char('(') && languageName != QLatin1String("text"))
                out.append({i, end - i, Kind::Function});
            else if (lang.capitalTypes && word.at(0).isUpper() && word.size() > 1)
                out.append({i, end - i, Kind::Type});
            i = end;
            continue;
        }
        ++i;
    }
    return out;
}

QString toHtml(const QString &code, const QString &languageName, const QVariantMap &colors)
{
    static const char *const names[] = {"keyword", "type", "string", "number", "comment", "preprocessor", "function"};
    QString html;
    qsizetype at = 0;
    for (const Span &span : spans(code, languageName)) {
        html += code.mid(at, span.start - at).toHtmlEscaped();
        const QString piece = code.mid(span.start, span.length).toHtmlEscaped();
        const QColor color = colors.value(QLatin1String(names[int(span.kind)])).value<QColor>();
        const bool italic = span.kind == Kind::Comment;
        if (color.isValid()) {
            html += QStringLiteral("<span style=\"color:%1;%2\">%3</span>")
                        .arg(color.name(), italic ? QStringLiteral(" font-style:italic;") : QString(), piece);
        } else {
            html += piece;
        }
        at = span.start + span.length;
    }
    html += code.mid(at).toHtmlEscaped();
    return html;
}

} // namespace Highlight
