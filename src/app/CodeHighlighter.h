// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QColor>
#include <QObject>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>

namespace talos {

/// Syntax colouring for a code block's text document.
///
/// One tokenizer covers the languages chat replies actually contain: it
/// knows comments, strings, numbers and a per-family keyword list, which is
/// enough to make code readable without shipping a grammar per language.
/// QML sets `document` to a TextEdit's `textDocument` and `language` to the
/// fence's info string.
class CodeHighlighter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
public:
    explicit CodeHighlighter(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    bool dark() const { return m_dark; }
    void setDark(bool dark);

signals:
    void documentChanged();
    void languageChanged();
    void darkChanged();

private:
    class Impl;
    void apply();

    QQuickTextDocument *m_document = nullptr;
    Impl *m_impl = nullptr;
    QString m_language;
    bool m_dark = false;
};

} // namespace talos
