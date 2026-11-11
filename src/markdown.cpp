#include "markdown.h"

#include <QRegularExpression>
#include <QUrl>

#include <functional>

namespace Markdown {

namespace {

const QRegularExpression kTask(QStringLiteral(R"(^(\s*)(?:[-*+]|\d+[.)])\s+\[( |x|X)\]\s+(.*)$)"));
const QRegularExpression kWiki(QStringLiteral(R"((!?)\[\[([^\[\]|]+)(?:\|([^\[\]]*))?\]\])"));
const QRegularExpression kFenceOpen(QStringLiteral(R"(^\s{0,3}(```+|~~~+)\s*([^`\s]*)?.*$)"));

// Runs f(lineIndex, line) for each line outside fenced code
template<typename F>
void forProseLines(const QStringList &lines, F f)
{
    QString fence;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (fence.isEmpty()) {
            const QRegularExpressionMatch m = kFenceOpen.match(lines.at(i));
            if (m.hasMatch()) {
                fence = m.captured(1);
                continue;
            }
            f(i, lines.at(i));
        } else if (trimmed.startsWith(fence) && trimmed.count(fence.at(0)) == trimmed.size()) {
            fence.clear();
        }
    }
}

// Replaces wiki links outside `inline code` on one line
template<typename F>
QString replaceLinks(const QString &line, F replacement)
{
    QString out;
    qsizetype at = 0;
    bool inCode = false;
    QString plain;
    const auto flush = [&] {
        qsizetype pos = 0;
        for (auto it = kWiki.globalMatch(plain); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            out += plain.mid(pos, m.capturedStart() - pos);
            out += replacement(m);
            pos = m.capturedEnd();
        }
        out += plain.mid(pos);
        plain.clear();
    };
    for (; at < line.size(); ++at) {
        const QChar c = line.at(at);
        if (c == QLatin1Char('`')) {
            if (!inCode)
                flush();
            inCode = !inCode;
            out += c;
            continue;
        }
        if (inCode)
            out += c;
        else
            plain += c;
    }
    flush();
    return out;
}

QString transform(const QString &markdown, const std::function<QString(const QRegularExpressionMatch &)> &f)
{
    QStringList lines = markdown.split(QLatin1Char('\n'));
    forProseLines(lines, [&](int i, const QString &line) {
        if (line.contains(QLatin1String("[[")))
            lines[i] = replaceLinks(line, f);
    });
    return lines.join(QLatin1Char('\n'));
}

} // namespace

QList<Task> tasks(const QString &markdown)
{
    QList<Task> result;
    forProseLines(markdown.split(QLatin1Char('\n')), [&](int i, const QString &line) {
        const QRegularExpressionMatch m = kTask.match(line);
        if (m.hasMatch())
            result.append({i, m.captured(2) != QLatin1String(" "), int(m.captured(1).size()), m.captured(3)});
    });
    return result;
}

QString toggleTask(const QString &markdown, int n)
{
    const QList<Task> all = tasks(markdown);
    if (n < 0 || n >= all.size())
        return markdown;
    QStringList lines = markdown.split(QLatin1Char('\n'));
    QString &line = lines[all.at(n).line];
    const qsizetype box = line.indexOf(QLatin1Char('['));
    if (box >= 0 && box + 2 < line.size())
        line[box + 1] = all.at(n).done ? QLatin1Char(' ') : QLatin1Char('x');
    return lines.join(QLatin1Char('\n'));
}

QList<WikiLink> wikiLinks(const QString &markdown)
{
    QList<WikiLink> result;
    forProseLines(markdown.split(QLatin1Char('\n')), [&](int i, const QString &line) {
        if (!line.contains(QLatin1String("[[")))
            return;
        replaceLinks(line, [&](const QRegularExpressionMatch &m) {
            WikiLink link;
            link.embed = !m.captured(1).isEmpty();
            link.target = m.captured(2).trimmed();
            link.alias = m.captured(3).trimmed();
            link.line = i;
            link.standalone = line.trimmed() == m.captured();
            result.append(link);
            return m.captured();
        });
    });
    return result;
}

QString setEmbed(const QString &markdown, int line, bool embed)
{
    QStringList lines = markdown.split(QLatin1Char('\n'));
    if (line < 0 || line >= lines.size())
        return markdown;
    QString &text = lines[line];
    const qsizetype at = text.indexOf(QLatin1String("[["));
    if (at < 0)
        return markdown;
    const bool isEmbed = at > 0 && text.at(at - 1) == QLatin1Char('!');
    if (embed && !isEmbed)
        text.insert(at, QLatin1Char('!'));
    else if (!embed && isEmbed)
        text.remove(at - 1, 1);
    return lines.join(QLatin1Char('\n'));
}

QString linkify(const QString &markdown)
{
    return transform(markdown, [](const QRegularExpressionMatch &m) {
        const QString target = m.captured(2).trimmed();
        QString shown = m.captured(3).trimmed();
        if (shown.isEmpty())
            shown = target;
        shown.replace(QLatin1Char('['), QLatin1Char('(')).replace(QLatin1Char(']'), QLatin1Char(')'));
        return QStringLiteral("[%1](note:%2)").arg(shown, QString::fromUtf8(QUrl::toPercentEncoding(target, "/")));
    });
}

QString unlinkify(const QString &markdown)
{
    return transform(markdown, [](const QRegularExpressionMatch &m) {
        const QString shown = m.captured(3).trimmed();
        return shown.isEmpty() ? m.captured(2).trimmed() : shown;
    });
}

QList<Fence> fences(const QString &markdown)
{
    QList<Fence> result;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = kFenceOpen.match(lines.at(i));
        if (!m.hasMatch())
            continue;
        const QString marker = m.captured(1);
        Fence fence;
        fence.startLine = i;
        fence.language = m.captured(2).trimmed().toLower();
        QStringList code;
        int j = i + 1;
        for (; j < lines.size(); ++j) {
            const QString trimmed = lines.at(j).trimmed();
            if (trimmed.startsWith(marker) && trimmed.count(marker.at(0)) == trimmed.size())
                break;
            code << lines.at(j);
        }
        fence.endLine = std::min<int>(j, int(lines.size()) - 1);
        fence.code = code.join(QLatin1Char('\n'));
        result.append(fence);
        i = j;
    }
    return result;
}

namespace {

// One line of CSV into its cells: quotes may hold separators and "" quotes
QStringList splitCsv(const QString &line, QChar separator)
{
    QStringList cells;
    QString cell;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"') && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                cell += c;
                ++i;
            } else if (c == QLatin1Char('"')) {
                quoted = false;
            } else {
                cell += c;
            }
        } else if (c == QLatin1Char('"') && cell.trimmed().isEmpty()) {
            quoted = true;
            cell.clear();
        } else if (c == separator) {
            cells << cell;
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells << cell;
    return cells;
}

} // namespace

QString tableFromCells(const QString &text, QChar separator)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
    }
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.size() < 2 || lines.size() > 500)
        return {};
    QList<QStringList> rows;
    for (const QString &line : std::as_const(lines)) {
        const QStringList cells = separator == QLatin1Char('\t') ? line.split(separator) : splitCsv(line, separator);
        if (cells.size() < 2 || (!rows.isEmpty() && cells.size() != rows.first().size()))
            return {};
        rows << cells;
    }
    const auto row = [](const QStringList &cells) {
        QStringList safe;
        for (QString c : cells)
            safe << c.trimmed().replace(QLatin1Char('|'), QStringLiteral("\\|"));
        return QStringLiteral("| ") + safe.join(QStringLiteral(" | ")) + QStringLiteral(" |");
    };
    QStringList out{row(rows.first())};
    out << QStringLiteral("|") + QStringList(rows.first().size(), QStringLiteral("---")).join(QLatin1Char('|'))
               + QStringLiteral("|");
    for (qsizetype i = 1; i < rows.size(); ++i)
        out << row(rows.at(i));
    return out.join(QLatin1Char('\n'));
}

bool isWebUrl(const QString &text)
{
    const QString t = text.trimmed();
    if (t.contains(QLatin1Char(' ')) || t.contains(QLatin1Char('\n')))
        return false;
    const QUrl url(t, QUrl::StrictMode);
    return url.isValid() && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))
           && !url.host().isEmpty();
}

QString link(const QString &text, const QString &url)
{
    QString shown = text.simplified();
    shown.replace(QLatin1Char('['), QLatin1Char('(')).replace(QLatin1Char(']'), QLatin1Char(')'));
    QString target = url;
    target.replace(QLatin1Char(' '), QStringLiteral("%20")).replace(QLatin1Char(')'), QStringLiteral("%29"));
    return QStringLiteral("[%1](%2)").arg(shown.isEmpty() ? url : shown, target);
}

} // namespace Markdown
