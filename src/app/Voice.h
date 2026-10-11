// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

class QTextToSpeech;

namespace talos {

class Store;

/// Speech out, and on Android speech in.
///
/// Replies are read aloud with the platform's own text-to-speech (through
/// Qt TextToSpeech). Listening uses Android's speech recogniser; Qt has no
/// speech-recognition API on desktop, so there `canListen` is false and the
/// voice screen only reads replies.
class Voice : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canSpeak READ canSpeak NOTIFY changed)
    Q_PROPERTY(bool canListen READ canListen CONSTANT)
    Q_PROPERTY(bool speaking READ speaking NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY changed)
public:
    explicit Voice(Store *store, QObject *parent = nullptr);
    ~Voice() override;

    bool canSpeak() const;
    bool canListen() const;
    bool speaking() const { return m_speaking; }
    bool listening() const { return m_listening; }
    QVariantList voices() const;

    /// Read Markdown aloud (code blocks and markup are left out).
    Q_INVOKABLE void speak(const QString &markdown);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void listen();
    Q_INVOKABLE void applySettings();

signals:
    void changed();
    void finishedSpeaking();
    void heard(const QString &text);
    void listenFailed(const QString &reason);

private:
    void ensureEngine();

    Store *m_store;
    QTextToSpeech *m_tts = nullptr;
    bool m_speaking = false;
    bool m_listening = false;
};

} // namespace talos
