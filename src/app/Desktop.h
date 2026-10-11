// SPDX-License-Identifier: GPL-2.0-only
#pragma once

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

    void notify(const QString &title, const QString &body);
    /// Make noise until someone finds the device.
    void ring(const QString &message);

    Q_INVOKABLE bool autostart() const;
    Q_INVOKABLE void setAutostart(bool on);

signals:
    void activateRequested();
    void newChatRequested();
    void quitRequested();

private:
    QString autostartPath() const;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QString m_ringFile;
    QPointer<QObject> m_ringPlayer;
};

} // namespace talos
