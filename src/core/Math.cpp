// SPDX-License-Identifier: GPL-2.0-only
#include "core/Math.h"

#include <QHash>

namespace talos::math {

using namespace Qt::StringLiterals;

namespace {

const QHash<QString, QString> &symbols()
{
    static const QHash<QString, QString> table{
        // Greek
        {u"alpha"_s, u"α"_s}, {u"beta"_s, u"β"_s}, {u"gamma"_s, u"γ"_s}, {u"delta"_s, u"δ"_s},
        {u"epsilon"_s, u"ε"_s}, {u"varepsilon"_s, u"ε"_s}, {u"zeta"_s, u"ζ"_s}, {u"eta"_s, u"η"_s},
        {u"theta"_s, u"θ"_s}, {u"vartheta"_s, u"ϑ"_s}, {u"iota"_s, u"ι"_s}, {u"kappa"_s, u"κ"_s},
        {u"lambda"_s, u"λ"_s}, {u"mu"_s, u"μ"_s}, {u"nu"_s, u"ν"_s}, {u"xi"_s, u"ξ"_s},
        {u"pi"_s, u"π"_s}, {u"rho"_s, u"ρ"_s}, {u"sigma"_s, u"σ"_s}, {u"tau"_s, u"τ"_s},
        {u"upsilon"_s, u"υ"_s}, {u"phi"_s, u"φ"_s}, {u"varphi"_s, u"φ"_s}, {u"chi"_s, u"χ"_s},
        {u"psi"_s, u"ψ"_s}, {u"omega"_s, u"ω"_s}, {u"Gamma"_s, u"Γ"_s}, {u"Delta"_s, u"Δ"_s},
        {u"Theta"_s, u"Θ"_s}, {u"Lambda"_s, u"Λ"_s}, {u"Xi"_s, u"Ξ"_s}, {u"Pi"_s, u"Π"_s},
        {u"Sigma"_s, u"Σ"_s}, {u"Phi"_s, u"Φ"_s}, {u"Psi"_s, u"Ψ"_s}, {u"Omega"_s, u"Ω"_s},
        // Operators and relations
        {u"times"_s, u"×"_s}, {u"cdot"_s, u"·"_s}, {u"div"_s, u"÷"_s}, {u"pm"_s, u"±"_s},
        {u"mp"_s, u"∓"_s}, {u"leq"_s, u"≤"_s}, {u"le"_s, u"≤"_s}, {u"geq"_s, u"≥"_s},
        {u"ge"_s, u"≥"_s}, {u"neq"_s, u"≠"_s}, {u"ne"_s, u"≠"_s}, {u"approx"_s, u"≈"_s},
        {u"equiv"_s, u"≡"_s}, {u"sim"_s, u"∼"_s}, {u"simeq"_s, u"≃"_s}, {u"propto"_s, u"∝"_s},
        {u"ll"_s, u"≪"_s}, {u"gg"_s, u"≫"_s}, {u"infty"_s, u"∞"_s}, {u"partial"_s, u"∂"_s},
        {u"nabla"_s, u"∇"_s}, {u"sum"_s, u"∑"_s}, {u"prod"_s, u"∏"_s}, {u"int"_s, u"∫"_s},
        {u"iint"_s, u"∬"_s}, {u"oint"_s, u"∮"_s}, {u"to"_s, u"→"_s}, {u"rightarrow"_s, u"→"_s},
        {u"leftarrow"_s, u"←"_s}, {u"gets"_s, u"←"_s}, {u"Rightarrow"_s, u"⇒"_s},
        {u"implies"_s, u"⇒"_s}, {u"Leftarrow"_s, u"⇐"_s}, {u"leftrightarrow"_s, u"↔"_s},
        {u"Leftrightarrow"_s, u"⇔"_s}, {u"iff"_s, u"⇔"_s}, {u"mapsto"_s, u"↦"_s},
        {u"uparrow"_s, u"↑"_s}, {u"downarrow"_s, u"↓"_s}, {u"in"_s, u"∈"_s}, {u"notin"_s, u"∉"_s},
        {u"ni"_s, u"∋"_s}, {u"subset"_s, u"⊂"_s}, {u"subseteq"_s, u"⊆"_s}, {u"supset"_s, u"⊃"_s},
        {u"supseteq"_s, u"⊇"_s}, {u"cup"_s, u"∪"_s}, {u"cap"_s, u"∩"_s}, {u"setminus"_s, u"∖"_s},
        {u"emptyset"_s, u"∅"_s}, {u"varnothing"_s, u"∅"_s}, {u"forall"_s, u"∀"_s},
        {u"exists"_s, u"∃"_s}, {u"neg"_s, u"¬"_s}, {u"lnot"_s, u"¬"_s}, {u"land"_s, u"∧"_s},
        {u"wedge"_s, u"∧"_s}, {u"lor"_s, u"∨"_s}, {u"vee"_s, u"∨"_s}, {u"oplus"_s, u"⊕"_s},
        {u"otimes"_s, u"⊗"_s}, {u"circ"_s, u"∘"_s}, {u"bullet"_s, u"•"_s}, {u"star"_s, u"⋆"_s},
        {u"ast"_s, u"∗"_s}, {u"perp"_s, u"⊥"_s}, {u"parallel"_s, u"∥"_s}, {u"angle"_s, u"∠"_s},
        {u"cdots"_s, u"⋯"_s}, {u"ldots"_s, u"…"_s}, {u"dots"_s, u"…"_s}, {u"vdots"_s, u"⋮"_s},
        {u"ddots"_s, u"⋱"_s}, {u"langle"_s, u"⟨"_s}, {u"rangle"_s, u"⟩"_s}, {u"lfloor"_s, u"⌊"_s},
        {u"rfloor"_s, u"⌋"_s}, {u"lceil"_s, u"⌈"_s}, {u"rceil"_s, u"⌉"_s}, {u"mid"_s, u"|"_s},
        {u"vert"_s, u"|"_s}, {u"Vert"_s, u"‖"_s}, {u"prime"_s, u"′"_s}, {u"hbar"_s, u"ℏ"_s},
        {u"ell"_s, u"ℓ"_s}, {u"Re"_s, u"ℜ"_s}, {u"Im"_s, u"ℑ"_s}, {u"aleph"_s, u"ℵ"_s},
        {u"colon"_s, u":"_s}, {u"degree"_s, u"°"_s}, {u"therefore"_s, u"∴"_s}, {u"because"_s, u"∵"_s},
        // Spacing and things that are only layout
        {u"quad"_s, u"  "_s}, {u"qquad"_s, u"    "_s}, {u"left"_s, QString()},
        {u"right"_s, QString()}, {u"big"_s, QString()}, {u"Big"_s, QString()},
        {u"bigg"_s, QString()}, {u"Bigg"_s, QString()}, {u"displaystyle"_s, QString()},
        {u"limits"_s, QString()}, {u"nolimits"_s, QString()},
    };
    return table;
}

/// Commands whose braces only style what is inside them.
bool isWrapper(const QString &name)
{
    static const QStringList wrappers{
        u"text"_s, u"mathrm"_s, u"mathbf"_s, u"mathit"_s, u"mathsf"_s, u"mathtt"_s,
        u"mathcal"_s, u"operatorname"_s, u"textbf"_s, u"textit"_s, u"boldsymbol"_s, u"bm"_s,
        u"vec"_s, u"hat"_s, u"bar"_s, u"tilde"_s, u"overline"_s, u"underline"_s, u"boxed"_s,
    };
    return wrappers.contains(name);
}

QString blackboard(const QString &inner)
{
    static const QHash<QString, QString> table{
        {u"R"_s, u"ℝ"_s}, {u"N"_s, u"ℕ"_s}, {u"Z"_s, u"ℤ"_s}, {u"Q"_s, u"ℚ"_s},
        {u"C"_s, u"ℂ"_s}, {u"P"_s, u"ℙ"_s}, {u"E"_s, u"𝔼"_s}, {u"H"_s, u"ℍ"_s},
    };
    return table.value(inner, inner);
}

/// Every character as a raised or lowered form, or nothing when one of them
/// has none; a formula half-raised reads worse than one written with ^.
QString scripted(const QString &text, bool raised)
{
    static const QHash<QChar, QString> up{
        {u'0', u"⁰"_s}, {u'1', u"¹"_s}, {u'2', u"²"_s}, {u'3', u"³"_s}, {u'4', u"⁴"_s},
        {u'5', u"⁵"_s}, {u'6', u"⁶"_s}, {u'7', u"⁷"_s}, {u'8', u"⁸"_s}, {u'9', u"⁹"_s},
        {u'+', u"⁺"_s}, {u'-', u"⁻"_s}, {u'−', u"⁻"_s}, {u'=', u"⁼"_s}, {u'(', u"⁽"_s},
        {u')', u"⁾"_s}, {u'n', u"ⁿ"_s}, {u'i', u"ⁱ"_s}, {u'a', u"ᵃ"_s}, {u'b', u"ᵇ"_s},
        {u'c', u"ᶜ"_s}, {u'd', u"ᵈ"_s}, {u'e', u"ᵉ"_s}, {u'k', u"ᵏ"_s}, {u'm', u"ᵐ"_s},
        {u't', u"ᵗ"_s}, {u'x', u"ˣ"_s}, {u'y', u"ʸ"_s}, {u'T', u"ᵀ"_s},
    };
    static const QHash<QChar, QString> down{
        {u'0', u"₀"_s}, {u'1', u"₁"_s}, {u'2', u"₂"_s}, {u'3', u"₃"_s}, {u'4', u"₄"_s},
        {u'5', u"₅"_s}, {u'6', u"₆"_s}, {u'7', u"₇"_s}, {u'8', u"₈"_s}, {u'9', u"₉"_s},
        {u'+', u"₊"_s}, {u'-', u"₋"_s}, {u'−', u"₋"_s}, {u'=', u"₌"_s}, {u'(', u"₍"_s},
        {u')', u"₎"_s}, {u'a', u"ₐ"_s}, {u'e', u"ₑ"_s}, {u'o', u"ₒ"_s}, {u'x', u"ₓ"_s},
        {u'i', u"ᵢ"_s}, {u'j', u"ⱼ"_s}, {u'k', u"ₖ"_s}, {u'm', u"ₘ"_s}, {u'n', u"ₙ"_s},
        {u't', u"ₜ"_s},
    };
    const auto &table = raised ? up : down;
    QString out;
    for (const QChar c : text) {
        const auto it = table.constFind(c);
        if (it == table.constEnd())
            return {};
        out += *it;
    }
    return out;
}

/// Whether a part of a fraction needs brackets to stay one thing.
bool isCompound(const QString &text)
{
    for (const QChar c : text) {
        if (c.isSpace() || u"+-−·×÷/=<>"_s.contains(c))
            return true;
    }
    return false;
}

class Reader
{
public:
    explicit Reader(const QString &tex) : m_tex(tex) {}

    /// The whole input. A closing brace with nothing to close is dropped
    /// rather than allowed to end the reading early.
    QString all()
    {
        QString out;
        while (m_at < m_tex.size()) {
            out += run();
            if (m_at < m_tex.size())
                ++m_at;
        }
        return out;
    }

    /// Everything up to an unmatched `}` or the end.
    QString run()
    {
        QString out;
        while (m_at < m_tex.size()) {
            const QChar c = m_tex.at(m_at);
            if (c == u'}')
                break;
            if (c == u'{') {
                out += group();
            } else if (c == u'\\') {
                out += command();
            } else if (c == u'^' || c == u'_') {
                ++m_at;
                const QString arg = argument();
                const QString mark = scripted(arg, c == u'^');
                if (!mark.isEmpty())
                    out += mark;
                else if (!arg.isEmpty())
                    out += c + (arg.size() > 1 ? u'(' + arg + u')' : arg);
            } else if (c == u'&') {
                out += u' ';
                ++m_at;
            } else if (c == u'~') {
                out += u' ';
                ++m_at;
            } else {
                out += c;
                ++m_at;
            }
        }
        return out;
    }

private:
    /// `{…}` with the braces consumed.
    QString group()
    {
        ++m_at; // {
        const QString inner = run();
        if (m_at < m_tex.size())
            ++m_at; // }
        return inner;
    }

    /// What a ^, _ or command applies to: a group, a command, or one character.
    QString argument()
    {
        while (m_at < m_tex.size() && m_tex.at(m_at) == u' ')
            ++m_at;
        if (m_at >= m_tex.size())
            return {};
        if (m_tex.at(m_at) == u'{')
            return group();
        if (m_tex.at(m_at) == u'\\')
            return command();
        return QString(m_tex.at(m_at++));
    }

    QString command()
    {
        ++m_at; // backslash
        if (m_at >= m_tex.size())
            return {};
        const QChar first = m_tex.at(m_at);
        if (!first.isLetter()) {
            ++m_at;
            if (first == u'\\')
                return u"\n"_s;
            if (first == u',' || first == u';' || first == u':' || first == u' ')
                return u" "_s;
            if (first == u'!')
                return {};
            return QString(first); // \{ \} \% \$ \_ …
        }
        qsizetype end = m_at;
        while (end < m_tex.size() && m_tex.at(end).isLetter())
            ++end;
        const QString name = m_tex.mid(m_at, end - m_at);
        m_at = end;

        if (name == "frac"_L1 || name == "dfrac"_L1 || name == "tfrac"_L1) {
            const QString top = argument(), bottom = argument();
            return (isCompound(top) ? u'(' + top + u')' : top) + u'/'
                + (isCompound(bottom) ? u'(' + bottom + u')' : bottom);
        }
        if (name == "sqrt"_L1) {
            QString index;
            if (m_at < m_tex.size() && m_tex.at(m_at) == u'[') {
                const qsizetype close = m_tex.indexOf(u']', m_at);
                if (close > 0) {
                    index = scripted(m_tex.mid(m_at + 1, close - m_at - 1), true);
                    m_at = close + 1;
                }
            }
            const QString inner = argument();
            return index + u'√' + (inner.size() > 1 ? u'(' + inner + u')' : inner);
        }
        if (name == "mathbb"_L1)
            return blackboard(argument());
        if (name == "begin"_L1 || name == "end"_L1) {
            argument(); // the environment's name says nothing a reader needs
            return {};
        }
        if (isWrapper(name))
            return argument();
        const auto it = symbols().constFind(name);
        if (it != symbols().constEnd())
            return *it;
        // sin, log, lim and friends, and anything not known: its name.
        return name;
    }

    const QString &m_tex;
    qsizetype m_at = 0;
};

} // namespace

QString toUnicode(const QString &tex)
{
    Reader reader(tex);
    QStringList lines;
    for (const QString &line : reader.all().split(u'\n'))
        lines << line.simplified();
    lines.removeAll(QString());
    return lines.join(u'\n');
}

} // namespace talos::math
