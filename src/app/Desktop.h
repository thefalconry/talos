// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPointer>

class QSystemTrayIcon;
class QMenu;

namespace talos {

/// The parts of the app that live outside its window: the tray icon,
/// system notifications, launch-at-login and the "find my device" ring.
class Desktop : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
    Q_PROPERTY(bool autostartSupported READ autostartSupported CONSTANT)
public:
    explicit Desktop(QObject *parent = nullptr);
    ~Desktop() override;

    /// The app mark, drawn here so no image format plugin is needed.
    static QIcon appIcon();

    bool trayAvailable() const;
    bool autostartSupported() const;
    void showTray(bool show, const QString &tooltip);
    void setTrayTooltip(const QString &tooltip);

    /// No notifications or sounds at all: for scripted runs, which would
    /// otherwise announce their test conversations on the real desktop.
    void setMuted(bool muted) { m_muted = muted; }
    void notify(const QString &title, const QString &body);
    /// A notification about a chat. Where the desktop allows it, clicking
    /// it opens that chat and, when @p canReply, an answer can be typed
    /// into the notification itself.
    void notifyChat(const QString &chatId, const QString &title, const QString &body,
                    bool canReply);
    /// Make noise until someone finds the device.
    void ring(const QString &message);

    Q_INVOKABLE bool autostart() const;
    Q_INVOKABLE void setAutostart(bool on);

signals:
    void activateRequested();
    void newChatRequested();
    void quitRequested();
    void openChatRequested(const QString &chatId);
    void replyRequested(const QString &chatId, const QString &text);

private slots:
    void onNotificationAction(uint id, const QString &action);
    void onNotificationReply(uint id, const QString &text);
    void onNotificationClosed(uint id, uint reason);

private:
    QString autostartPath() const;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QString m_ringFile;
    /// Notifications on screen, by the id the desktop gave them.
    QHash<uint, QString> m_notified;
    QStringList m_notifyCapabilities;
    bool m_notifyWired = false;
    bool m_muted = false;
    QPointer<QObject> m_ringPlayer;
};

} // namespace talos
