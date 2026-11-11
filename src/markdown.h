#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// What the app reads out of a note's Markdown beyond what QTextDocument
// renders: task lists, [[wiki links]] and fenced code. Plain functions,
// so the tests can use them on their own.
namespace Markdown {

// "- [ ] buy milk" and "1. [x] done": the task lists of GitHub Markdown.
// Lines inside fenced code don't count.
struct Task {
    int line = 0;   // 0-based line of the note
    bool done = false;
    int indent = 0; // leading spaces
    QString text;
};
QList<Task> tasks(const QString &markdown);
// The note with its n-th task (0-based) ticked or unticked
QString toggleTask(const QString &markdown, int n);

// [[Target]], [[Target|shown text]] and ![[Target]] (an embed: the note
// shows a preview of the target). Target may be "Project/Title".
struct WikiLink {
    QString target;
    QString alias;
    int line = 0;
    bool embed = false;      // written with "!"
    bool standalone = false; // alone on its line
};
QList<WikiLink> wikiLinks(const QString &markdown);
// Turns the link on `line` into an embed or back into a plain link
QString setEmbed(const QString &markdown, int line, bool embed);
// [[links]] as Markdown links to note:<target>, for rendering; left alone
// in code
QString linkify(const QString &markdown);
// [[links]] as just their text, for the clipboard
QString unlinkify(const QString &markdown);

// ```language ... ``` blocks
struct Fence {
    int startLine = 0; // the opening ``` line
    int endLine = 0;   // the closing one, or the last line if unclosed
    QString language;
    QString code;
};
QList<Fence> fences(const QString &markdown);

} // namespace Markdown
