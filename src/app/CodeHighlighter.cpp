// SPDX-License-Identifier: GPL-2.0-only
#include "app/CodeHighlighter.h"

#include <QSet>
#include <QTextDocument>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

struct Grammar {
    QSet<QString> keywords;
    QSet<QString> literals;
    bool slashComments = false;  // // and /* */
    bool hashComments = false;   // #
    bool dashComments = false;   // --
    bool htmlComments = false;   // <!-- -->
    bool backtickStrings = false;
    bool singleQuoteStrings = true;
    bool caseInsensitive = false;
    bool markup = false;         // tags instead of keywords
    bool diff = false;
    bool plain = false;
};

QSet<QString> words(const char *list)
{
    QSet<QString> out;
    for (const QString &w : QString::fromLatin1(list).split(u' ', Qt::SkipEmptyParts))
        out.insert(w);
    return out;
}

const Grammar &grammarFor(const QString &language)
{
    static const QSet<QString> commonLiterals = words("true false null nil None True False undefined NULL nullptr");
    static const Grammar cLike{
        words("auto break case catch class const constexpr continue default delete do else enum "
              "explicit export extern final for friend goto if inline namespace new noexcept "
              "operator override private protected public return sizeof static struct switch "
              "template this throw try typedef typename union using virtual void volatile while "
              "int long short char bool float double unsigned signed size_t string var let "
              "function async await yield import from as interface type implements extends "
              "package func defer go chan map range select fn impl trait pub mut use mod crate "
              "match where self Self super abstract synchronized throws instanceof val fun "
              "object companion data sealed when is in out get set readonly declare keyof "
              "typeof of static final dyn move ref unsafe loop guard protocol extension"),
        commonLiterals, true, false, false, false, true, true, false, false, false, false};
    static const Grammar scripting{
        words("and as assert async await break class continue def del elif else except finally "
              "for from global if import in is lambda nonlocal not or pass raise return try "
              "while with yield match case self begin end do then fi done esac function local "
              "export echo exit unless until module require include elsif rescue ensure puts "
              "print set unset readonly declare let var const if else foreach in"),
        commonLiterals, false, true, false, false, true, true, false, false, false, false};
    static const Grammar sql{
        words("select from where and or not insert into values update set delete create table "
              "index view drop alter add column primary key foreign references join left right "
              "inner outer full on group by order having limit offset as distinct union all "
              "case when then else end is null like in between exists begin commit rollback "
              "with returning default unique constraint if int integer text varchar boolean "
              "timestamp real numeric"),
        words("true false null"), false, false, true, false, false, true, true, false, false, false};
    static const Grammar data{{}, commonLiterals, false, true, false, false, false, true, false,
                              false, false, false};
    static const Grammar json{{}, words("true false null"), true, false, false, false, false, false,
                              false, false, false, false};
    static const Grammar markup{{}, {}, false, false, false, true, false, true, false, true, false,
                                false};
    static const Grammar diff{{}, {}, false, false, false, false, false, false, false, false, true,
                              false};
    static const Grammar plain{{}, {}, false, false, false, false, false, false, false, false,
                               false, true};

    static const QHash<QString, const Grammar *> byName{
        {u"c"_s, &cLike}, {u"cpp"_s, &cLike}, {u"c++"_s, &cLike}, {u"h"_s, &cLike},
        {u"hpp"_s, &cLike}, {u"cc"_s, &cLike}, {u"cs"_s, &cLike}, {u"csharp"_s, &cLike},
        {u"java"_s, &cLike}, {u"kotlin"_s, &cLike}, {u"kt"_s, &cLike}, {u"swift"_s, &cLike},
        {u"js"_s, &cLike}, {u"javascript"_s, &cLike}, {u"jsx"_s, &cLike}, {u"ts"_s, &cLike},
        {u"typescript"_s, &cLike}, {u"tsx"_s, &cLike}, {u"go"_s, &cLike}, {u"golang"_s, &cLike},
        {u"rust"_s, &cLike}, {u"rs"_s, &cLike}, {u"dart"_s, &cLike}, {u"qml"_s, &cLike},
        {u"php"_s, &cLike}, {u"scala"_s, &cLike}, {u"zig"_s, &cLike}, {u"css"_s, &cLike},
        {u"scss"_s, &cLike}, {u"glsl"_s, &cLike}, {u"objc"_s, &cLike}, {u"proto"_s, &cLike},
        {u"py"_s, &scripting}, {u"python"_s, &scripting}, {u"sh"_s, &scripting},
        {u"bash"_s, &scripting}, {u"zsh"_s, &scripting}, {u"shell"_s, &scripting},
        {u"fish"_s, &scripting}, {u"console"_s, &scripting}, {u"rb"_s, &scripting},
        {u"ruby"_s, &scripting}, {u"pl"_s, &scripting}, {u"perl"_s, &scripting},
        {u"r"_s, &scripting}, {u"make"_s, &scripting}, {u"makefile"_s, &scripting},
        {u"cmake"_s, &scripting}, {u"dockerfile"_s, &scripting}, {u"docker"_s, &scripting},
        {u"powershell"_s, &scripting}, {u"ps1"_s, &scripting}, {u"nix"_s, &scripting},
        {u"elixir"_s, &scripting}, {u"julia"_s, &scripting},
        {u"yaml"_s, &data}, {u"yml"_s, &data}, {u"toml"_s, &data}, {u"ini"_s, &data},
        {u"conf"_s, &data}, {u"env"_s, &data}, {u"properties"_s, &data},
        {u"json"_s, &json}, {u"jsonc"_s, &json}, {u"json5"_s, &json},
        {u"sql"_s, &sql}, {u"sqlite"_s, &sql}, {u"postgres"_s, &sql}, {u"mysql"_s, &sql},
        {u"lua"_s, &sql}, {u"haskell"_s, &sql},
        {u"html"_s, &markup}, {u"xml"_s, &markup}, {u"svg"_s, &markup}, {u"vue"_s, &markup},
        {u"diff"_s, &diff}, {u"patch"_s, &diff},
        {u"text"_s, &plain}, {u"txt"_s, &plain}, {u"plaintext"_s, &plain}, {u"output"_s, &plain},
        {u"log"_s, &plain}, {u"md"_s, &plain}, {u"markdown"_s, &plain},
    };
    const auto it = byName.constFind(language.toLower());
    // An unnamed fence is usually code or a shell session: colour strings,
    // numbers and both comment styles, which is rarely wrong.
    static const Grammar guess{cLike.keywords, commonLiterals, true, true, false, false, true, true,
                               false, false, false, false};
    return it == byName.constEnd() ? guess : **it;
}

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'_';
}

} // namespace

class CodeHighlighter::Impl : public QSyntaxHighlighter
{
public:
    explicit Impl(QTextDocument *doc) : QSyntaxHighlighter(doc) {}

    void configure(const QString &language, bool dark)
    {
        m_grammar = &grammarFor(language);
        const auto fmt = [](const QColor &c, bool italic = false) {
            QTextCharFormat f;
            f.setForeground(c);
            f.setFontItalic(italic);
            return f;
        };
        // Muted, warm palette: enough contrast to scan, quiet enough to read.
        m_comment = fmt(dark ? QColor(0x8A, 0x86, 0x7C) : QColor(0x8C, 0x86, 0x7A), true);
        m_string = fmt(dark ? QColor(0xA8, 0xC9, 0x8A) : QColor(0x4F, 0x7A, 0x28));
        m_number = fmt(dark ? QColor(0xE0, 0xA9, 0x6B) : QColor(0xA8, 0x5A, 0x12));
        m_keyword = fmt(dark ? QColor(0xD8, 0x8F, 0xB8) : QColor(0xA6, 0x3D, 0x7A));
        m_literal = fmt(dark ? QColor(0x7F, 0xB8, 0xD8) : QColor(0x1F, 0x6F, 0x9C));
        m_call = fmt(dark ? QColor(0x8F, 0xB4, 0xE8) : QColor(0x2B, 0x5C, 0xB0));
        m_added = fmt(dark ? QColor(0x8F, 0xCF, 0x8F) : QColor(0x2E, 0x7D, 0x32));
        m_removed = fmt(dark ? QColor(0xE8, 0x8A, 0x8A) : QColor(0xC6, 0x28, 0x28));
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override
    {
        const Grammar &g = *m_grammar;
        if (g.plain)
            return;
        if (g.diff) {
            if (text.startsWith(u'+') && !text.startsWith("+++"_L1))
                setFormat(0, int(text.size()), m_added);
            else if (text.startsWith(u'-') && !text.startsWith("---"_L1))
                setFormat(0, int(text.size()), m_removed);
            else if (text.startsWith("@@"_L1))
                setFormat(0, int(text.size()), m_literal);
            else if (text.startsWith("+++"_L1) || text.startsWith("---"_L1)
                     || text.startsWith("diff "_L1))
                setFormat(0, int(text.size()), m_comment);
            return;
        }

        enum State { Normal = -1, BlockComment = 1, HtmlComment = 2, TripleDouble = 3, TripleSingle = 4 };
        int state = previousBlockState();
        const int n = int(text.size());
        int i = 0;

        const auto continueUntil = [&](QLatin1StringView closer, const QTextCharFormat &f, int open) {
            const int end = int(text.indexOf(closer, i));
            if (end < 0) {
                setFormat(i, n - i, f);
                i = n;
                state = open;
            } else {
                setFormat(i, end + int(closer.size()) - i, f);
                i = end + int(closer.size());
                state = Normal;
            }
        };

        if (state == BlockComment)
            continueUntil("*/"_L1, m_comment, BlockComment);
        else if (state == HtmlComment)
            continueUntil("-->"_L1, m_comment, HtmlComment);
        else if (state == TripleDouble)
            continueUntil("\"\"\""_L1, m_string, TripleDouble);
        else if (state == TripleSingle)
            continueUntil("'''"_L1, m_string, TripleSingle);

        while (i < n) {
            const QChar c = text.at(i);
            const QStringView rest = QStringView(text).sliced(i);

            if (g.slashComments && rest.startsWith("//"_L1)) {
                setFormat(i, n - i, m_comment);
                break;
            }
            if (g.slashComments && rest.startsWith("/*"_L1)) {
                const int start = i;
                i += 2;
                continueUntil("*/"_L1, m_comment, BlockComment);
                setFormat(start, 2, m_comment);
                continue;
            }
            if (g.htmlComments && rest.startsWith("<!--"_L1)) {
                const int start = i;
                i += 4;
                continueUntil("-->"_L1, m_comment, HtmlComment);
                setFormat(start, 4, m_comment);
                continue;
            }
            // `#` starts a comment only at a word boundary, so `a#b` and
            // colour codes inside values survive.
            if (g.hashComments && c == u'#' && (i == 0 || text.at(i - 1).isSpace())) {
                setFormat(i, n - i, m_comment);
                break;
            }
            if (g.dashComments && rest.startsWith("--"_L1)) {
                setFormat(i, n - i, m_comment);
                break;
            }
            if (g.hashComments && (rest.startsWith("\"\"\""_L1) || rest.startsWith("'''"_L1))) {
                const bool dbl = c == u'"';
                const int start = i;
                i += 3;
                continueUntil(dbl ? "\"\"\""_L1 : "'''"_L1, m_string, dbl ? TripleDouble : TripleSingle);
                setFormat(start, 3, m_string);
                continue;
            }
            if (c == u'"' || (c == u'\'' && g.singleQuoteStrings)
                || (c == u'`' && g.backtickStrings)) {
                int j = i + 1;
                while (j < n && text.at(j) != c) {
                    if (text.at(j) == u'\\')
                        ++j;
                    ++j;
                }
                const int len = qMin(j + 1, n) - i;
                // A lone apostrophe in prose-like code (Rust lifetimes,
                // contractions in comments) is not a string.
                if (c == u'\'' && j >= n) {
                    ++i;
                    continue;
                }
                bool key = false;
                if (m_grammar->keywords.isEmpty() && !g.markup) {
                    int k = i + len;
                    while (k < n && text.at(k).isSpace())
                        ++k;
                    key = k < n && text.at(k) == u':';
                }
                setFormat(i, len, key ? m_call : m_string);
                i += len;
                continue;
            }
            if (g.markup && c == u'<') {
                int j = i + 1;
                if (j < n && text.at(j) == u'/')
                    ++j;
                const int nameStart = j;
                while (j < n && (isWordChar(text.at(j)) || text.at(j) == u'-' || text.at(j) == u':'))
                    ++j;
                if (j > nameStart)
                    setFormat(nameStart, j - nameStart, m_keyword);
                i = qMax(j, i + 1);
                continue;
            }
            if (c.isDigit() && (i == 0 || !isWordChar(text.at(i - 1)))) {
                int j = i + 1;
                while (j < n && (text.at(j).isLetterOrNumber() || text.at(j) == u'.'
                                 || text.at(j) == u'_'))
                    ++j;
                setFormat(i, j - i, m_number);
                i = j;
                continue;
            }
            if (c.isLetter() || c == u'_') {
                int j = i + 1;
                while (j < n && isWordChar(text.at(j)))
                    ++j;
                const QString word = text.mid(i, j - i);
                const QString probe = g.caseInsensitive ? word.toLower() : word;
                if (g.keywords.contains(probe)) {
                    setFormat(i, j - i, m_keyword);
                } else if (g.literals.contains(probe)) {
                    setFormat(i, j - i, m_literal);
                } else if (!g.markup && j < n && text.at(j) == u'(') {
                    setFormat(i, j - i, m_call);
                } else if (m_grammar->keywords.isEmpty() && !g.markup) {
                    // key: value / key = value in config formats
                    int k = j;
                    while (k < n && text.at(k) == u' ')
                        ++k;
                    if (k < n && (text.at(k) == u':' || text.at(k) == u'=')
                        && text.left(i).trimmed().remove(u'-').trimmed().isEmpty())
                        setFormat(i, j - i, m_call);
                }
                i = j;
                continue;
            }
            ++i;
        }
        setCurrentBlockState(state);
    }

private:
    const Grammar *m_grammar = &grammarFor(QString());
    QTextCharFormat m_comment, m_string, m_number, m_keyword, m_literal, m_call, m_added, m_removed;
};

CodeHighlighter::CodeHighlighter(QObject *parent) : QObject(parent) {}

void CodeHighlighter::setDocument(QQuickTextDocument *document)
{
    if (document == m_document)
        return;
    delete m_impl;
    m_impl = nullptr;
    m_document = document;
    if (document && document->textDocument()) {
        m_impl = new Impl(document->textDocument());
        apply();
    }
    emit documentChanged();
}

void CodeHighlighter::setLanguage(const QString &language)
{
    if (language == m_language)
        return;
    m_language = language;
    apply();
    emit languageChanged();
}

void CodeHighlighter::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    apply();
    emit darkChanged();
}

void CodeHighlighter::apply()
{
    if (m_impl)
        m_impl->configure(m_language, m_dark);
}

} // namespace talos
