// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>
#include <QVariantList>

namespace talos::markdown {

/// Split a message into renderable blocks: runs of prose (`type: "text"`,
/// handed to the rich-text renderer as Markdown) and fenced code blocks
/// (`type: "code"` with `lang`, `code` and `closed`), which get their own
/// component with a header, a copy button and syntax highlighting.
///
/// A fence still open at the end of the text, as happens mid-stream, yields
/// a code block with `closed: false` rather than leaking backticks into the
/// prose.
QVariantList split(const QString &text);

/// Markdown reduced to what should be read aloud: no code blocks, no markup.
QString toSpeech(const QString &text);

} // namespace talos::markdown
