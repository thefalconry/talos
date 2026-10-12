// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QObject>
#include <QVariantMap>

#if defined(TALOS_HAVE_DBUS)
#  include <QDBusObjectPath>
#endif

namespace talos {

/// One system-wide keyboard shortcut, asked for through the desktop's
/// GlobalShortcuts portal (KDE Plasma, GNOME 48+, Hyprland).
///
/// The desktop owns the binding: it shows the user which keys are being
/// asked for and lets them choose others, and nothing is grabbed behind its
/// back. Where there is no such portal `available` is false, and the same
/// thing is reached by binding a key to `talos --quick` in the desktop's
/// own shortcut settings.
class GlobalShortcut : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString trigger READ trigger NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit GlobalShortcut(QObject *parent = nullptr);

    bool available() const { return m_available; }
    /// off | binding | bound | failed
    QString state() const { return m_state; }
    /// What the desktop says the keys are, once bound.
    QString trigger() const { return m_trigger; }
    QString error() const { return m_error; }

    Q_INVOKABLE void bind();
    Q_INVOKABLE void release();

signals:
    void changed();
    void activated();

#if defined(TALOS_HAVE_DBUS)
private slots:
    void onCreated(uint code, const QVariantMap &results);
    void onBound(uint code, const QVariantMap &results);
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp,
                     const QVariantMap &options);
#endif

private:
    void set(const QString &state, const QString &error = {});
    QString requestPath(const QString &token) const;

    bool m_available = false;
    QString m_state = QStringLiteral("off");
    QString m_trigger;
    QString m_error;
    QString m_session;
    int m_tokens = 0;
};

} // namespace talos
