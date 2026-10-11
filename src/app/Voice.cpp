// SPDX-License-Identifier: GPL-2.0-only
#include "app/Voice.h"

#include "core/AppLog.h"
#include "core/Markdown.h"
#include "core/Store.h"

#include <QRegularExpression>

#if defined(TALOS_HAVE_TTS)
#  include <QTextToSpeech>
#  include <QVoice>
#endif

#if defined(Q_OS_ANDROID)
#  include <QCoreApplication>
#  include <QJniObject>
#  include <QtCore/private/qandroidextras_p.h>
#endif

namespace talos {

using namespace Qt::StringLiterals;

Voice::Voice(Store *store, QObject *parent) : QObject(parent), m_store(store) {}

Voice::~Voice() = default;

void Voice::ensureEngine()
{
#if defined(TALOS_HAVE_TTS)
    if (m_tts)
        return;
    // Created on first use: probing speech engines costs a noticeable
    // moment at startup on some platforms.
    m_tts = new QTextToSpeech(this);
    connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State state) {
        const bool now = state == QTextToSpeech::Speaking || state == QTextToSpeech::Synthesizing;
        if (now == m_speaking)
            return;
        m_speaking = now;
        emit changed();
        if (!now)
            emit finishedSpeaking();
    });
    applySettings();
    emit changed();
#endif
}

bool Voice::canSpeak() const
{
#if defined(TALOS_HAVE_TTS)
    return !QTextToSpeech::availableEngines().isEmpty();
#else
    return false;
#endif
}

bool Voice::canListen() const
{
#if defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}

QVariantList Voice::voices() const
{
    QVariantList out;
#if defined(TALOS_HAVE_TTS)
    if (!m_tts)
        return out;
    for (const QVoice &v : m_tts->availableVoices()) {
        out << QVariantMap{
            {u"name"_s, v.name()},
            {u"detail"_s, QVoice::genderName(v.gender()) + u" · "_s + v.locale().nativeLanguageName()},
        };
    }
#endif
    return out;
}

void Voice::applySettings()
{
#if defined(TALOS_HAVE_TTS)
    ensureEngine();
    if (!m_tts)
        return;
    m_tts->setRate(qBound(-1.0, m_store->pref(u"voiceRate"_s).toDouble(), 1.0));
    const QString wanted = m_store->prefString(u"voiceName"_s);
    if (wanted.isEmpty())
        return;
    for (const QVoice &v : m_tts->availableVoices()) {
        if (v.name() == wanted) {
            m_tts->setVoice(v);
            break;
        }
    }
#endif
}

void Voice::speak(const QString &markdown)
{
#if defined(TALOS_HAVE_TTS)
    ensureEngine();
    const QString text = markdown::toSpeech(markdown);
    if (!m_tts || text.isEmpty()) {
        emit finishedSpeaking();
        return;
    }
    m_tts->stop();
    // Sentence-sized pieces: some engines cut one very long utterance off.
    static const QRegularExpression boundary(u"(?<=[.!?…])\\s+|\\n+"_s);
    const QStringList pieces = text.split(boundary, Qt::SkipEmptyParts);
    for (const QString &piece : pieces)
        m_tts->enqueue(piece.trimmed());
#else
    Q_UNUSED(markdown);
    emit finishedSpeaking();
#endif
}

void Voice::stop()
{
#if defined(TALOS_HAVE_TTS)
    if (m_tts)
        m_tts->stop();
#endif
}

void Voice::listen()
{
#if defined(Q_OS_ANDROID)
    if (m_listening)
        return;
    // Android's own recognition activity: it owns the microphone
    // permission prompt and the listening UI, and returns the best match.
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      QJniObject::fromString(u"android.speech.action.RECOGNIZE_SPEECH"_s)
                          .object<jstring>());
    intent.callObjectMethod(
        "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        QJniObject::fromString(u"android.speech.extra.LANGUAGE_MODEL"_s).object<jstring>(),
        QJniObject::fromString(u"free_form"_s).object<jstring>());
    m_listening = true;
    emit changed();
    QtAndroidPrivate::startActivity(intent, 0x7A10,
                                    [this](int, int resultCode, const QJniObject &data) {
        QString text;
        if (resultCode == -1 && data.isValid()) { // Activity.RESULT_OK
            const QJniObject results = data.callObjectMethod(
                "getStringArrayListExtra", "(Ljava/lang/String;)Ljava/util/ArrayList;",
                QJniObject::fromString(u"android.speech.extra.RESULTS"_s).object<jstring>());
            if (results.isValid() && results.callMethod<jint>("size") > 0) {
                text = results.callObjectMethod("get", "(I)Ljava/lang/Object;", 0).toString();
            }
        }
        QMetaObject::invokeMethod(this, [this, text] {
            m_listening = false;
            emit changed();
            if (text.trimmed().isEmpty())
                emit listenFailed(u"Nothing was heard"_s);
            else
                emit heard(text.trimmed());
        }, Qt::QueuedConnection);
    });
#else
    emit listenFailed(u"Speech input isn't available on this platform"_s);
#endif
}

} // namespace talos
