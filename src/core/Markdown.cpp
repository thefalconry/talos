// SPDX-License-Identifier: GPL-2.0-only
#include "core/Markdown.h"

#include <QRegularExpression>

namespace talos::markdown {

using namespace Qt::StringLiterals;

namespace {

struct Fence {
    QChar ch;
    int length = 0;
    QString info;
};

/// An opening or closing code fence: up to three spaces of indent, then at
/// least three backticks or tildes.
bool parseFence(QStringView line, Fence *out)
{
    int i = 0;
    while (i < line.size() && i < 3 && line.at(i) == u' ')
        ++i;
    if (i >= line.size())
        return false;
    const QChar ch = line.at(i);
    if (ch != u'`' && ch != u'~')
        return false;
    int n = 0;
    while (i + n < line.size() && line.at(i + n) == ch)
        ++n;
    if (n < 3)
        return false;
    const QString info = line.sliced(i + n).trimmed().toString();
    // A backtick fence's info string cannot itself contain backticks; that
    // is inline code which happens to start a line.
    if (ch == u'`' && info.contains(u'`'))
        return false;
    out->ch = ch;
    out->length = n;
    out->info = info;
    return true;
}

/// Make prose safe to hand to a rich-text item.
///
/// The text item would fetch every image the Markdown names, and render any
/// HTML in it. Neither should happen by merely displaying a reply, so an
/// image becomes an ordinary link the reader can choose to open, and an HTML
/// tag is shown as the characters it is. Inline code is left alone.
QString disarm(const QString &text)
{
    static const QRegularExpression image(
        u"\\G!\\[([^\\]\\n]*)\\]\\(\\s*<?([^)\\s>]+)>?[^)\\n]*\\)"_s);
    static const QRegularExpression autolink(
        u"\\G<(?:https?://|mailto:)[^\\s<>]*>"_s, QRegularExpression::CaseInsensitiveOption);
    QString out;
    out.reserve(text.size() + 16);
    qsizetype i = 0;
    const qsizetype n = text.size();
    while (i < n) {
        const QChar c = text.at(i);
        if (c == u'`') {
            // A code span: a run of backticks up to the next run of the
            // same length. Without one, the backticks are just characters.
            qsizetype run = 1;
            while (i + run < n && text.at(i + run) == u'`')
                ++run;
            qsizetype close = -1;
            for (qsizetype j = i + run; j < n;) {
                if (text.at(j) != u'`') {
                    ++j;
                    continue;
                }
                qsizetype r = 1;
                while (j + r < n && text.at(j + r) == u'`')
                    ++r;
                if (r == run) {
                    close = j;
                    break;
                }
                j += r;
            }
            const qsizetype end = close < 0 ? i + run : close + run;
            out += QStringView(text).sliced(i, end - i);
            i = end;
            continue;
        }
        if (c == u'\\' && i + 1 < n) {
            // An escaped character stays escaped.
            out += c;
            out += text.at(i + 1);
            i += 2;
            continue;
        }
        if (c == u'!') {
            const auto m = image.match(text, i);
            if (m.hasMatch()) {
                const QString alt = m.captured(1).trimmed();
                out += u'[' + (alt.isEmpty() ? u"image"_s : alt) + u"]("_s + m.captured(2) + u')';
                i = m.capturedEnd();
                continue;
            }
        }
        if (c == u'<' && i + 1 < n) {
            const QChar next = text.at(i + 1);
            const auto link = autolink.match(text, i);
            if (link.hasMatch()) {
                out += link.capturedView();
                i = link.capturedEnd();
                continue;
            }
            if (next.isLetter() || next == u'/' || next == u'!' || next == u'?') {
                out += u"&lt;"_s;
                ++i;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

void pushText(QVariantList &blocks, QStringList &lines)
{
    const QString text = disarm(lines.join(u'\n').trimmed());
    lines.clear();
    if (text.isEmpty())
        return;
    blocks.append(QVariantMap{{u"type"_s, u"text"_s}, {u"text"_s, text}});
}

} // namespace

QVariantList split(const QString &text)
{
    QVariantList blocks;
    QStringList prose;
    QStringList code;
    bool inCode = false;
    Fence open;

    const auto lines = QStringView(text).split(u'\n');
    for (QStringView raw : lines) {
        QStringView line = raw;
        if (line.endsWith(u'\r'))
            line.chop(1);
        Fence f;
        const bool isFence = parseFence(line, &f);
        if (!inCode) {
            if (isFence) {
                pushText(blocks, prose);
                inCode = true;
                open = f;
                code.clear();
            } else {
                prose << line.toString();
            }
            continue;
        }
        if (isFence && f.ch == open.ch && f.length >= open.length && f.info.isEmpty()) {
            blocks.append(QVariantMap{
                {u"type"_s, u"code"_s},
                {u"lang"_s, open.info.section(u' ', 0, 0).toLower()},
                {u"code"_s, code.join(u'\n')},
                {u"closed"_s, true},
            });
            inCode = false;
            continue;
        }
        code << line.toString();
    }
    if (inCode) {
        blocks.append(QVariantMap{
            {u"type"_s, u"code"_s},
            {u"lang"_s, open.info.section(u' ', 0, 0).toLower()},
            {u"code"_s, code.join(u'\n')},
            {u"closed"_s, false},
        });
    } else {
        pushText(blocks, prose);
    }
    return blocks;
}

QString toSpeech(const QString &text)
{
    QString out;
    const QVariantList blocks = split(text);
    for (const QVariant &b : blocks) {
        const QVariantMap m = b.toMap();
        if (m.value(u"type"_s).toString() == "code"_L1) {
            out += u" Code block omitted. "_s;
            continue;
        }
        out += m.value(u"text"_s).toString() + u'\n';
    }
    // split() showed HTML tags as text; say them as they were written.
    out.replace(u"&lt;"_s, u"<"_s);
    static const QRegularExpression image(u"!\\[([^\\]]*)\\]\\([^)]*\\)"_s);
    out.replace(image, u"\\1"_s);
    static const QRegularExpression link(u"\\[([^\\]]+)\\]\\([^)]*\\)"_s);
    out.replace(link, u"\\1"_s);
    static const QRegularExpression heading(u"^\\s{0,3}#{1,6}\\s+"_s,
                                            QRegularExpression::MultilineOption);
    out.remove(heading);
    static const QRegularExpression bullet(u"^\\s*([-*+]|\\d+[.)])\\s+"_s,
                                           QRegularExpression::MultilineOption);
    out.remove(bullet);
    static const QRegularExpression quote(u"^\\s*>+\\s?"_s, QRegularExpression::MultilineOption);
    out.remove(quote);
    static const QRegularExpression tableRule(u"^\\s*\\|?[\\s:|-]+\\|[\\s:|-]*$"_s,
                                              QRegularExpression::MultilineOption);
    out.remove(tableRule);
    static const QRegularExpression marks(u"[*_~`|]+"_s);
    out.replace(marks, u" "_s);
    static const QRegularExpression url(u"https?://\\S+"_s);
    out.replace(url, u"link"_s);
    static const QRegularExpression spaces(u"[ \\t]+"_s);
    out.replace(spaces, u" "_s);
    return out.trimmed();
}

} // namespace talos::markdown
