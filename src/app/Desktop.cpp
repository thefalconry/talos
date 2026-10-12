// SPDX-License-Identifier: GPL-2.0-only
#include "app/Desktop.h"

#include "core/AppLog.h"
#include "mesh/Platform.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QtMath>

#if defined(TALOS_HAVE_DBUS)
#  include <QDBusConnection>
#  include <QDBusInterface>
#  include <QDBusMessage>
#  include <QDBusPendingCallWatcher>
#  include <QDBusPendingReply>
#  include <QDBusReply>
#endif
#if defined(TALOS_HAVE_MULTIMEDIA)
#  include <QSoundEffect>
#endif
#if defined(Q_OS_MACOS)
#  include "app/DesktopMac.h"
#  include <QKeyEvent>
#  include <QMenuBar>
#  include <QWindow>
#endif

namespace talos {

using namespace Qt::StringLiterals;

Desktop::Desktop(QObject *parent) : QObject(parent)
{
#if defined(Q_OS_MACOS)
    // Before the app finishes starting, so a click on a notification that
    // started it is not lost.
    mac::setupNotifications(this);
#endif
}

Desktop::~Desktop()
{
    delete m_menu;
    delete m_menuBar;
    if (!m_ringFile.isEmpty())
        QFile::remove(m_ringFile);
}

QIcon Desktop::appIcon()
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 64.0, size / 64.0);
        // The same geometry as assets/talos.svg.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x1F, 0x1D, 0x1A));
        p.drawRoundedRect(QRectF(2, 2, 60, 60), 15, 15);
        QPainterPath talon;
        talon.moveTo(17, 20);
        talon.cubicTo(30, 16, 44, 22, 47, 36);
        talon.cubicTo(48, 42, 45, 47, 40, 49);
        talon.cubicTo(43, 42, 40, 35, 33, 32);
        talon.cubicTo(36, 38, 33, 44, 27, 46);
        talon.cubicTo(30, 39, 26, 33, 19, 31);
        talon.cubicTo(22, 28, 22, 24, 17, 20);
        talon.closeSubpath();
        p.setBrush(QColor(0xE0, 0x7A, 0x45));
        p.drawPath(talon);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QIcon Desktop::trayIcon()
{
#if defined(Q_OS_MACOS)
    QIcon icon;
    for (const int size : {18, 36, 54}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        // The claw of appIcon() without its tile, filling the square.
        p.scale(size / 36.0, size / 36.0);
        p.translate(-14, -15);
        QPainterPath talon;
        talon.moveTo(17, 20);
        talon.cubicTo(30, 16, 44, 22, 47, 36);
        talon.cubicTo(48, 42, 45, 47, 40, 49);
        talon.cubicTo(43, 42, 40, 35, 33, 32);
        talon.cubicTo(36, 38, 33, 44, 27, 46);
        talon.cubicTo(30, 39, 26, 33, 19, 31);
        talon.cubicTo(22, 28, 22, 24, 17, 20);
        talon.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawPath(talon);
        p.end();
        pm.setDevicePixelRatio(size / 18.0);
        icon.addPixmap(pm);
    }
    icon.setIsMask(true);
    return icon;
#else
    return appIcon();
#endif
}

void Desktop::installMenuBar()
{
#if defined(Q_OS_MACOS)
    if (m_menuBar)
        return;
    // Parentless: the one menu bar every window of the app shares.
    auto *bar = new QMenuBar(nullptr);
    m_menuBar = bar;
    const auto command = [this](QMenu *menu, const QString &title, const QString &name,
                                const QKeySequence &keys = {}) {
        QAction *action = menu->addAction(title);
        action->setShortcut(keys);
        connect(action, &QAction::triggered, this, [this, name] { emit menuRequested(name); });
        return action;
    };
    // What a text field would do with the keys, for someone who uses the
    // menu instead: the field in focus gets the key press itself.
    const auto editing = [](QMenu *menu, const QString &title, QKeySequence::StandardKey keys) {
        QAction *action = menu->addAction(title);
        action->setShortcut(keys);
        connect(action, &QAction::triggered, action, [keys] {
            QWindow *window = QGuiApplication::focusWindow();
            const QKeySequence sequence(keys);
            if (!window || sequence.isEmpty())
                return;
            const QKeyCombination combo = sequence[0];
            QKeyEvent press(QEvent::KeyPress, combo.key(), combo.keyboardModifiers());
            QCoreApplication::sendEvent(window, &press);
            QKeyEvent release(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers());
            QCoreApplication::sendEvent(window, &release);
        });
    };

    QMenu *file = bar->addMenu(tr("File"));
    // Qt moves these three to the application menu, where a Mac keeps them.
    command(file, tr("About Talos"), u"about"_s)->setMenuRole(QAction::AboutRole);
    command(file, tr("Settings…"), u"settings"_s, QKeySequence::Preferences)
        ->setMenuRole(QAction::PreferencesRole);
    QAction *quit = file->addAction(tr("Quit Talos"));
    quit->setMenuRole(QAction::QuitRole);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &Desktop::quitRequested);
    QAction *fresh = file->addAction(tr("New Chat"));
    fresh->setShortcut(QKeySequence::New);
    connect(fresh, &QAction::triggered, this, &Desktop::newChatRequested);
    command(file, tr("Search and Commands…"), u"palette"_s, QKeySequence(Qt::CTRL | Qt::Key_K));
    file->addSeparator();
    command(file, tr("Close Window"), u"close"_s, QKeySequence::Close);

    QMenu *edit = bar->addMenu(tr("Edit"));
    editing(edit, tr("Undo"), QKeySequence::Undo);
    editing(edit, tr("Redo"), QKeySequence::Redo);
    edit->addSeparator();
    editing(edit, tr("Cut"), QKeySequence::Cut);
    editing(edit, tr("Copy"), QKeySequence::Copy);
    editing(edit, tr("Paste"), QKeySequence::Paste);
    editing(edit, tr("Select All"), QKeySequence::SelectAll);
    edit->addSeparator();
    command(edit, tr("Find in Chat…"), u"find"_s, QKeySequence::Find);

    QMenu *view = bar->addMenu(tr("View"));
    command(view, tr("Show or Hide Sidebar"), u"sidebar"_s, QKeySequence(Qt::CTRL | Qt::Key_B));

    QMenu *window = bar->addMenu(tr("Window"));
    command(window, tr("Minimize"), u"minimize"_s, QKeySequence(Qt::CTRL | Qt::Key_M));
    command(window, tr("Zoom"), u"zoom"_s);

    QMenu *help = bar->addMenu(tr("Help"));
    command(help, tr("Keyboard Shortcuts"), u"shortcuts"_s, QKeySequence(Qt::CTRL | Qt::Key_Slash));
#endif
}

bool Desktop::trayAvailable() const
{
    return !platform::isMobile() && QSystemTrayIcon::isSystemTrayAvailable();
}

void Desktop::showTray(bool show, const QString &tooltip)
{
    if (!trayAvailable())
        return;
    if (!show) {
        if (m_tray)
            m_tray->hide();
        return;
    }
    if (!m_tray) {
        m_tray = new QSystemTrayIcon(trayIcon(), this);
        m_menu = new QMenu;
        connect(m_menu->addAction(u"Open Talos"_s), &QAction::triggered, this,
                &Desktop::activateRequested);
        connect(m_menu->addAction(u"New chat"_s), &QAction::triggered, this,
                &Desktop::newChatRequested);
        m_menu->addSeparator();
        connect(m_menu->addAction(u"Quit"_s), &QAction::triggered, this, &Desktop::quitRequested);
        m_tray->setContextMenu(m_menu);
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                emit activateRequested();
        });
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, &Desktop::activateRequested);
    }
    m_tray->setToolTip(tooltip);
    m_tray->show();
}

void Desktop::setTrayTooltip(const QString &tooltip)
{
    if (m_tray)
        m_tray->setToolTip(tooltip);
}

void Desktop::notify(const QString &title, const QString &body)
{
    if (m_muted)
        return;
    if (platform::notifyNative(title, body))
        return;
#if defined(Q_OS_MACOS)
    if (mac::postNotification({}, title, body, false))
        return;
#endif
#if defined(TALOS_HAVE_DBUS)
    // The desktop notification service works whether or not there is a tray.
    QDBusInterface service(u"org.freedesktop.Notifications"_s, u"/org/freedesktop/Notifications"_s,
                           u"org.freedesktop.Notifications"_s, QDBusConnection::sessionBus());
    if (service.isValid()) {
        service.asyncCall(u"Notify"_s, u"Talos"_s, uint(0), u"dev.talos.client"_s, title, body,
                          QStringList(), QVariantMap{{u"desktop-entry"_s, u"dev.talos.client"_s}},
                          8000);
        return;
    }
#endif
    if (m_tray && m_tray->isVisible() && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(title, body, appIcon(), 8000);
}

void Desktop::notifyChat(const QString &chatId, const QString &title, const QString &body,
                         bool canReply)
{
    if (m_muted)
        return;
#if defined(TALOS_HAVE_DBUS)
    if (!platform::isMobile()) {
        const QString service = u"org.freedesktop.Notifications"_s;
        const QString path = u"/org/freedesktop/Notifications"_s;
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!m_notifyWired) {
            m_notifyWired = true;
            QDBusInterface probe(service, path, service, bus);
            const QDBusReply<QStringList> caps = probe.call(u"GetCapabilities"_s);
            if (caps.isValid())
                m_notifyCapabilities = caps.value();
            bus.connect(service, path, service, u"ActionInvoked"_s, this,
                        SLOT(onNotificationAction(uint, QString)));
            bus.connect(service, path, service, u"NotificationReplied"_s, this,
                        SLOT(onNotificationReply(uint, QString)));
            bus.connect(service, path, service, u"NotificationClosed"_s, this,
                        SLOT(onNotificationClosed(uint, uint)));
        }
        if (m_notifyCapabilities.contains(u"actions"_s)) {
            QStringList actions{u"default"_s, u"Open"_s};
            QVariantMap hints{{u"desktop-entry"_s, u"dev.talos.client"_s}};
            if (canReply && m_notifyCapabilities.contains(u"inline-reply"_s)) {
                actions << u"inline-reply"_s << u"Reply"_s;
                hints[u"x-kde-reply-placeholder-text"_s] = u"Reply to Talon"_s;
            }
            // Where the body is read as markup, a reply's own angle
            // brackets must arrive as text.
            const QString shown = m_notifyCapabilities.contains(u"body-markup"_s)
                ? body.toHtmlEscaped() : body;
            QDBusMessage call = QDBusMessage::createMethodCall(service, path, service, u"Notify"_s);
            call.setArguments({u"Talos"_s, uint(0), u"dev.talos.client"_s, title, shown, actions,
                               hints, 12'000});
            auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this,
                    [this, chatId](QDBusPendingCallWatcher *w) {
                const QDBusPendingReply<uint> reply = *w;
                w->deleteLater();
                if (!reply.isError())
                    m_notified.insert(reply.value(), chatId);
            });
            return;
        }
    }
#elif defined(Q_OS_MACOS)
    if (mac::postNotification(chatId, title, body, canReply))
        return;
#else
    Q_UNUSED(chatId);
    Q_UNUSED(canReply);
#endif
    notify(title, body);
}

void Desktop::onNotificationAction(uint id, const QString &action)
{
    const QString chatId = m_notified.value(id);
    if (!chatId.isEmpty() && action != "inline-reply"_L1)
        emit openChatRequested(chatId);
}

void Desktop::onNotificationReply(uint id, const QString &text)
{
    const QString chatId = m_notified.value(id);
    if (!chatId.isEmpty() && !text.trimmed().isEmpty())
        emit replyRequested(chatId, text);
}

void Desktop::onNotificationClosed(uint id, uint)
{
    // The reply signal comes just before this one, so nothing is lost.
    m_notified.remove(id);
}

void Desktop::ring(const QString &message)
{
    if (m_muted)
        return;
    notify(u"Talon is looking for this device"_s,
           message.isEmpty() ? u"Someone asked Talon to ring it."_s : message);
    if (platform::ringNative())
        return;
#if defined(TALOS_HAVE_MULTIMEDIA)
    if (m_ringFile.isEmpty()) {
        // Eight two-tone chirps, synthesised once: no audio asset to ship.
        constexpr int rate = 22050;
        QByteArray pcm;
        QDataStream samples(&pcm, QIODevice::WriteOnly);
        samples.setByteOrder(QDataStream::LittleEndian);
        for (int beep = 0; beep < 8; ++beep) {
            for (int i = 0; i < rate * 45 / 100; ++i) {
                const double t = double(i) / rate;
                const bool on = t < 0.28;
                const double freq = t < 0.14 ? 880.0 : 1175.0;
                const double envelope = on ? qMin(1.0, qMin(t, 0.28 - t) * 60.0) : 0.0;
                samples << qint16(envelope * 0.6 * 32767 * qSin(2 * M_PI * freq * t));
            }
        }
        QByteArray wav;
        QDataStream out(&wav, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + pcm.size());
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2)
            << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(pcm.size());
        wav += pcm;
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(u"talos-ring-%1.wav"_s.arg(QCoreApplication::applicationPid()));
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(wav);
            m_ringFile = path;
        }
    }
    if (!m_ringFile.isEmpty()) {
        delete m_ringPlayer;
        auto *effect = new QSoundEffect(this);
        effect->setSource(QUrl::fromLocalFile(m_ringFile));
        effect->setVolume(1.0);
        effect->play();
        m_ringPlayer = effect;
        return;
    }
#endif
    for (int i = 0; i < 8; ++i)
        QTimer::singleShot(i * 450, this, [] { QApplication::beep(); });
}

// ── Launch at login ─────────────────────────────────────────────────────────

bool Desktop::autostartSupported() const
{
    return !platform::isMobile() && !platform::isSandboxed();
}

QString Desktop::autostartPath() const
{
#if defined(Q_OS_MACOS)
    return QDir::home().filePath(u"Library/LaunchAgents/dev.talos.client.plist"_s);
#elif defined(Q_OS_LINUX)
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(u"autostart/dev.talos.client.desktop"_s);
#else
    return {};
#endif
}

bool Desktop::autostart() const
{
#if defined(Q_OS_WIN)
    const QSettings run(u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"_s,
                        QSettings::NativeFormat);
    return run.contains(u"Talos"_s);
#else
    const QString path = autostartPath();
    return !path.isEmpty() && QFile::exists(path);
#endif
}

void Desktop::setAutostart(bool on)
{
    if (!autostartSupported())
        return;
    // Inside an AppImage the binary's own path is a mount that changes
    // every launch; the AppImage file is the thing to start.
    QString exe = qEnvironmentVariable("APPIMAGE");
    if (exe.isEmpty())
        exe = QCoreApplication::applicationFilePath();
#if defined(Q_OS_WIN)
    QSettings run(u"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"_s,
                  QSettings::NativeFormat);
    if (on)
        run.setValue(u"Talos"_s, u"\"%1\" --hidden"_s.arg(QDir::toNativeSeparators(exe)));
    else
        run.remove(u"Talos"_s);
#else
    const QString path = autostartPath();
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        AppLog::warn("desktop", u"couldn't write %1"_s.arg(path));
        return;
    }
#  if defined(Q_OS_MACOS)
    f.write(u"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>\n"
            "  <key>Label</key><string>dev.talos.client</string>\n"
            "  <key>ProgramArguments</key><array><string>%1</string>"
            "<string>--hidden</string></array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "</dict></plist>\n"_s.arg(exe.toHtmlEscaped()).toUtf8());
#  else
    QString quoted = exe;
    quoted.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s);
    f.write(u"[Desktop Entry]\nType=Application\nName=Talos\n"
            "Exec=\"%1\" --hidden\nIcon=dev.talos.client\nX-GNOME-Autostart-enabled=true\n"_s
                .arg(quoted).toUtf8());
#  endif
#endif
}

} // namespace talos
