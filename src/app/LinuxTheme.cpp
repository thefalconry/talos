// SPDX-License-Identifier: GPL-2.0-only
#include "app/LinuxTheme.h"

#include "core/AppLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#if defined(TALOS_HAVE_DBUS)
#  include <QDBusArgument>
#  include <QDBusConnection>
#  include <QDBusInterface>
#  include <QDBusMessage>
#  include <QDBusReply>
#  include <QDBusVariant>
#endif

namespace talos {

using namespace Qt::StringLiterals;

LinuxTheme *LinuxTheme::instance()
{
    static LinuxTheme s_instance;
    return &s_instance;
}

bool LinuxTheme::isAvailable()
{
#if defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

LinuxTheme::LinuxTheme(QObject *parent)
    : QObject(parent)
{
#if defined(Q_OS_LINUX)
    m_desktop = detectDesktop();
#endif
}

LinuxTheme::~LinuxTheme()
{
    stopMonitoring();
}

#if defined(Q_OS_LINUX)

LinuxTheme::DesktopEnv LinuxTheme::detectDesktop()
{
    const QString session = QString::fromLatin1(qgetenv("XDG_SESSION_DESKTOP")).toLower();
    const QString current = QString::fromLatin1(qgetenv("XDG_CURRENT_DESKTOP")).toLower();
    const QString desktopSession = QString::fromLatin1(qgetenv("DESKTOP_SESSION")).toLower();
    const QString all = session + u':' + current + u':' + desktopSession;

    if (all.contains("kde"_L1) || all.contains("plasma"_L1))
        return DesktopEnv::Kde;
    if (all.contains("gnome"_L1) || all.contains("ubuntu"_L1) || all.contains("unity"_L1)
        || all.contains("cinnamon"_L1) || all.contains("pop"_L1) || all.contains("pantheon"_L1))
        return DesktopEnv::Gnome;
    if (all.contains("lxde"_L1))
        return DesktopEnv::Lxde;
    return DesktopEnv::Other;
}

QColor LinuxTheme::parseRgbString(const QString &input)
{
    const QString cleaned = input.trimmed();
    if (cleaned.isEmpty())
        return {};
    const QStringList parts = cleaned.split(u',');
    if (parts.size() >= 3) {
        bool okR = false, okG = false, okB = false;
        const int r = parts[0].trimmed().toInt(&okR);
        const int g = parts[1].trimmed().toInt(&okG);
        const int b = parts[2].trimmed().toInt(&okB);
        if (okR && okG && okB) {
            return QColor(std::clamp(r, 0, 255),
                          std::clamp(g, 0, 255),
                          std::clamp(b, 0, 255));
        }
    }
    return {};
}

LinuxTheme::Brightness LinuxTheme::parseKdeColorScheme(const QString &name)
{
    if (name.trimmed().isEmpty())
        return Brightness::Unknown;
    const QString lower = name.trimmed().toLower();
    if (lower.contains("dark"_L1) || lower.contains("black"_L1)
        || lower.contains("night"_L1) || lower.contains("inverse"_L1)) {
        return Brightness::Dark;
    }
    if (lower.contains("light"_L1) || lower.contains("white"_L1))
        return Brightness::Light;
    return Brightness::Unknown;
}

LinuxTheme::Brightness LinuxTheme::parseKdeGlobalsColorScheme(const QString &content)
{
    bool inGeneral = false;
    const QStringList lines = content.split(u'\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            inGeneral = (line.compare(u"[General]"_s, Qt::CaseInsensitive) == 0);
            continue;
        }
        if (inGeneral && line.startsWith("ColorScheme="_L1)) {
            const QString value = line.mid(12).trimmed();
            return parseKdeColorScheme(value);
        }
    }
    return Brightness::Unknown;
}

QColor LinuxTheme::parseKdeGlobalsAccent(const QString &content)
{
    bool inGeneral = false;
    const QStringList lines = content.split(u'\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            inGeneral = (line.compare(u"[General]"_s, Qt::CaseInsensitive) == 0);
            continue;
        }
        if (inGeneral && line.startsWith("AccentColor="_L1)) {
            const QString value = line.mid(12).trimmed();
            return parseRgbString(value);
        }
    }
    return {};
}

LinuxTheme::Brightness LinuxTheme::parseGnomeColorScheme(const QString &input)
{
    QString val = input.trimmed().toLower();
    val.remove(u'\'').remove(u'"');
    if (val == "prefer-dark"_L1 || val.contains("dark"_L1))
        return Brightness::Dark;
    if (val == "prefer-light"_L1 || val == "default"_L1)
        return Brightness::Light;
    return Brightness::Unknown;
}

QColor LinuxTheme::parseGnomeAccent(const QString &input)
{
    QString cleaned = input.trimmed().toLower();
    cleaned.remove(u'\'').remove(u'"');
    if (cleaned == "blue"_L1)
        return QColor(0x00, 0x73, 0xE5);
    if (cleaned == "teal"_L1)
        return QColor(0x30, 0x82, 0x80);
    if (cleaned == "green"_L1)
        return QColor(0x4B, 0x85, 0x01);
    if (cleaned == "yellow"_L1)
        return QColor(0xC8, 0x88, 0x00);
    if (cleaned == "orange"_L1)
        return QColor(0xE9, 0x54, 0x20);
    if (cleaned == "red"_L1)
        return QColor(0xDA, 0x34, 0x50);
    if (cleaned == "pink"_L1)
        return QColor(0xB3, 0x4C, 0xB3);
    if (cleaned == "purple"_L1)
        return QColor(0x77, 0x64, 0xD8);
    if (cleaned == "slate"_L1)
        return QColor(0x65, 0x7B, 0x69);
    if (cleaned == "brown"_L1)
        return QColor(0xB3, 0x91, 0x69);
    return {};
}

QColor LinuxTheme::parseLxdeDesktopConf(const QString &content)
{
    const QStringList lines = content.split(u'\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (!line.startsWith("sGtk/ColorScheme"_L1))
            continue;
        const int eq = line.indexOf(u'=');
        if (eq == -1)
            continue;
        const QString val = line.mid(eq + 1);
        const QStringList parts = val.split("\\n"_L1);
        for (const QString &part : parts) {
            if (part.startsWith("selected_bg_color:#"_L1) || part.startsWith("selected_bg_color: #"_L1)) {
                const QString hex = part.section(u'#', 1, 1).trimmed();
                if (hex.length() == 6) {
                    bool ok = false;
                    const uint rgb = hex.toUInt(&ok, 16);
                    if (ok)
                        return QColor::fromRgb(rgb);
                } else if (hex.length() >= 10) {
                    bool rOk = false, gOk = false, bOk = false;
                    const int r = hex.mid(0, 2).toInt(&rOk, 16);
                    const int g = hex.mid(4, 2).toInt(&gOk, 16);
                    const int b = hex.mid(8, 2).toInt(&bOk, 16);
                    if (rOk && gOk && bOk)
                        return QColor(r, g, b);
                }
            }
        }
    }
    return {};
}

LinuxTheme::Brightness LinuxTheme::parseGtkSettings(const QString &content)
{
    const QStringList lines = content.split(u'\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith("gtk-application-prefer-dark-theme"_L1)) {
            const QString val = line.section(u'=', 1, 1).trimmed().toLower();
            if (val == "1"_L1 || val == "true"_L1)
                return Brightness::Dark;
            if (val == "0"_L1 || val == "false"_L1)
                return Brightness::Light;
        }
        if (line.startsWith("gtk-theme-name"_L1)) {
            const QString name = line.section(u'=', 1, 1).trimmed().toLower();
            if (name.contains("dark"_L1))
                return Brightness::Dark;
        }
    }
    return Brightness::Unknown;
}

static QString runQuickCommand(const QString &cmd, const QStringList &args)
{
    QProcess proc;
    proc.start(cmd, args);
    if (!proc.waitForFinished(400)) {
        proc.kill();
        return {};
    }
    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0)
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return {};
}

LinuxTheme::Brightness LinuxTheme::queryKdeBrightness()
{
    const QString home = QDir::homePath();
    const QString configDir = qEnvironmentVariable("XDG_CONFIG_HOME", home + u"/.config"_s);
    QFile f(configDir + u"/kdeglobals"_s);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const Brightness b = parseKdeGlobalsColorScheme(QString::fromUtf8(f.readAll()));
        if (b != Brightness::Unknown)
            return b;
    }

    QString out = runQuickCommand(u"kreadconfig6"_s,
                                  {u"--file"_s, u"kdeglobals"_s, u"--group"_s, u"General"_s,
                                   u"--key"_s, u"ColorScheme"_s});
    if (out.isEmpty()) {
        out = runQuickCommand(u"kreadconfig5"_s,
                              {u"--file"_s, u"kdeglobals"_s, u"--group"_s, u"General"_s,
                               u"--key"_s, u"ColorScheme"_s});
    }
    const Brightness b = parseKdeColorScheme(out);
    if (b != Brightness::Unknown)
        return b;

    return Brightness::Unknown;
}

QColor LinuxTheme::queryKdeAccent()
{
    const QString home = QDir::homePath();
    const QString configDir = qEnvironmentVariable("XDG_CONFIG_HOME", home + u"/.config"_s);
    QFile f(configDir + u"/kdeglobals"_s);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QColor c = parseKdeGlobalsAccent(QString::fromUtf8(f.readAll()));
        if (c.isValid())
            return c;
    }

    QString out = runQuickCommand(u"kreadconfig6"_s,
                                  {u"--key"_s, u"AccentColor"_s, u"--group"_s, u"General"_s});
    if (out.isEmpty()) {
        out = runQuickCommand(u"kreadconfig5"_s,
                              {u"--key"_s, u"AccentColor"_s, u"--group"_s, u"General"_s});
    }
    return parseRgbString(out);
}

LinuxTheme::Brightness LinuxTheme::queryGnomeBrightness()
{
    const QString out = runQuickCommand(u"gsettings"_s,
                                        {u"get"_s, u"org.gnome.desktop.interface"_s,
                                         u"color-scheme"_s});
    Brightness b = parseGnomeColorScheme(out);
    if (b != Brightness::Unknown)
        return b;

    const QString theme = runQuickCommand(u"gsettings"_s,
                                          {u"get"_s, u"org.gnome.desktop.interface"_s,
                                           u"gtk-theme"_s});
    return parseGnomeColorScheme(theme);
}

QColor LinuxTheme::queryGnomeAccent()
{
    const QString out = runQuickCommand(u"gsettings"_s,
                                        {u"get"_s, u"org.gnome.desktop.interface"_s,
                                         u"accent-color"_s});
    return parseGnomeAccent(out);
}

QColor LinuxTheme::queryLxdeAccent()
{
    const QString home = QDir::homePath();
    QFile f(home + u"/.config/lxsession/LXDE/desktop.conf"_s);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        return parseLxdeDesktopConf(QString::fromUtf8(f.readAll()));
    return {};
}

LinuxTheme::Brightness LinuxTheme::queryPortalBrightness()
{
#if defined(TALOS_HAVE_DBUS)
    QDBusInterface portal(u"org.freedesktop.portal.Desktop"_s,
                          u"/org/freedesktop/portal/desktop"_s,
                          u"org.freedesktop.portal.Settings"_s,
                          QDBusConnection::sessionBus());
    if (portal.isValid()) {
        const QDBusReply<QDBusVariant> reply = portal.call(
            u"Read"_s, u"org.freedesktop.appearance"_s, u"color-scheme"_s);
        if (reply.isValid()) {
            const uint code = reply.value().variant().toUInt();
            if (code == 1)
                return Brightness::Dark;
            if (code == 2)
                return Brightness::Light;
        }
    }
#endif
    return Brightness::Unknown;
}

QColor LinuxTheme::queryPortalAccent()
{
#if defined(TALOS_HAVE_DBUS)
    QDBusInterface portal(u"org.freedesktop.portal.Desktop"_s,
                          u"/org/freedesktop/portal/desktop"_s,
                          u"org.freedesktop.portal.Settings"_s,
                          QDBusConnection::sessionBus());
    if (portal.isValid()) {
        const QDBusReply<QDBusVariant> reply = portal.call(
            u"Read"_s, u"org.freedesktop.appearance"_s, u"accent-color"_s);
        if (reply.isValid()) {
            const QVariant val = reply.value().variant();
            if (val.canConvert<QDBusArgument>()) {
                const QDBusArgument arg = val.value<QDBusArgument>();
                if (arg.currentType() == QDBusArgument::StructureType) {
                    arg.beginStructure();
                    double r = 0, g = 0, b = 0;
                    arg >> r >> g >> b;
                    arg.endStructure();
                    return QColor::fromRgbF(std::clamp(r, 0.0, 1.0),
                                            std::clamp(g, 0.0, 1.0),
                                            std::clamp(b, 0.0, 1.0));
                }
            } else if (val.canConvert<QVariantList>()) {
                const auto list = val.toList();
                if (list.size() >= 3) {
                    return QColor::fromRgbF(std::clamp(list[0].toDouble(), 0.0, 1.0),
                                            std::clamp(list[1].toDouble(), 0.0, 1.0),
                                            std::clamp(list[2].toDouble(), 0.0, 1.0));
                }
            }
        }
    }
#endif
    return {};
}

LinuxTheme::Brightness LinuxTheme::queryGtkSettingsBrightness()
{
    const QString home = QDir::homePath();
    const QString configDir = qEnvironmentVariable("XDG_CONFIG_HOME", home + u"/.config"_s);
    QFile f(configDir + u"/gtk-3.0/settings.ini"_s);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return parseGtkSettings(QString::fromUtf8(f.readAll()));
    }
    return Brightness::Unknown;
}

LinuxTheme::Brightness LinuxTheme::queryEnvBrightness()
{
    const QString gtkTheme = QString::fromLatin1(qgetenv("GTK_THEME")).toLower();
    if (gtkTheme.contains("dark"_L1))
        return Brightness::Dark;
    if (gtkTheme.contains("light"_L1))
        return Brightness::Light;
    return Brightness::Unknown;
}

#endif // Q_OS_LINUX

void LinuxTheme::initSync()
{
#if defined(Q_OS_LINUX)
    const QString home = QDir::homePath();
    const QString configDir = qEnvironmentVariable("XDG_CONFIG_HOME", home + u"/.config"_s);
    QFile kdeFile(configDir + u"/kdeglobals"_s);
    if (kdeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(kdeFile.readAll());
        const Brightness b = parseKdeGlobalsColorScheme(content);
        if (b != Brightness::Unknown)
            m_brightness = b;
        const QColor a = parseKdeGlobalsAccent(content);
        if (a.isValid())
            m_accentColor = a;
    }

    if (m_brightness == Brightness::Unknown)
        m_brightness = queryGtkSettingsBrightness();
    if (m_brightness == Brightness::Unknown)
        m_brightness = queryEnvBrightness();
#endif
}

void LinuxTheme::startMonitoring()
{
#if defined(Q_OS_LINUX)
    if (m_monitoring)
        return;
    m_monitoring = true;

#  if defined(TALOS_HAVE_DBUS)
    if (!m_dbusConnected) {
        m_dbusConnected = QDBusConnection::sessionBus().connect(
            u"org.freedesktop.portal.Desktop"_s,
            u"/org/freedesktop/portal/desktop"_s,
            u"org.freedesktop.portal.Settings"_s,
            u"SettingChanged"_s,
            this,
            SLOT(onPortalSettingChanged(QString, QString, QDBusVariant)));
    }
#  endif

    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
                &LinuxTheme::onConfigFileChanged);
    }
    ensureWatches();

    if (!m_pollTimer) {
        m_pollTimer = new QTimer(this);
        m_pollTimer->setInterval(5000);
        connect(m_pollTimer, &QTimer::timeout, this, &LinuxTheme::checkPeriodic);
    }
    m_pollTimer->start();

    // Query in the background to refine any fast-init estimates.
    QTimer::singleShot(50, this, &LinuxTheme::refresh);
#endif
}

void LinuxTheme::stopMonitoring()
{
    m_monitoring = false;
    if (m_pollTimer) {
        m_pollTimer->stop();
        delete m_pollTimer;
        m_pollTimer = nullptr;
    }
    if (m_watcher) {
        delete m_watcher;
        m_watcher = nullptr;
    }
#if defined(Q_OS_LINUX) && defined(TALOS_HAVE_DBUS)
    if (m_dbusConnected) {
        QDBusConnection::sessionBus().disconnect(
            u"org.freedesktop.portal.Desktop"_s,
            u"/org/freedesktop/portal/desktop"_s,
            u"org.freedesktop.portal.Settings"_s,
            u"SettingChanged"_s,
            this,
            SLOT(onPortalSettingChanged(QString, QString, QDBusVariant)));
        m_dbusConnected = false;
    }
#endif
}

void LinuxTheme::ensureWatches()
{
#if defined(Q_OS_LINUX)
    if (!m_watcher)
        return;
    const QString home = QDir::homePath();
    const QString configDir = qEnvironmentVariable("XDG_CONFIG_HOME", home + u"/.config"_s);
    const QString kdePath = configDir + u"/kdeglobals"_s;
    const QString dconfPath = configDir + u"/dconf/user"_s;

    if (QFile::exists(kdePath) && !m_watcher->files().contains(kdePath))
        m_watcher->addPath(kdePath);
    if (QFile::exists(dconfPath) && !m_watcher->files().contains(dconfPath))
        m_watcher->addPath(dconfPath);
#endif
}

void LinuxTheme::onConfigFileChanged(const QString &path)
{
#if defined(Q_OS_LINUX)
    // Some tools write files atomically via rename, which may cause inotify
    // watches to drop. Re-watch the file if it still exists.
    if (m_watcher && QFile::exists(path) && !m_watcher->files().contains(path))
        m_watcher->addPath(path);
    refresh();
#else
    Q_UNUSED(path);
#endif
}

void LinuxTheme::checkPeriodic()
{
    ensureWatches();
    refresh();
}

#if defined(Q_OS_LINUX) && defined(TALOS_HAVE_DBUS)
void LinuxTheme::onPortalSettingChanged(const QString &ns, const QString &key,
                                        const QDBusVariant &value)
{
    if (ns != "org.freedesktop.appearance"_L1)
        return;

    if (key == "color-scheme"_L1) {
        const uint code = value.variant().toUInt();
        Brightness b = Brightness::Unknown;
        if (code == 1)
            b = Brightness::Dark;
        else if (code == 2)
            b = Brightness::Light;
        if (b != Brightness::Unknown && b != m_brightness) {
            m_brightness = b;
            emit brightnessChanged(m_brightness);
            emit themeChanged(m_brightness, m_accentColor);
        }
    } else if (key == "accent-color"_L1) {
        const QVariant val = value.variant();
        QColor color;
        if (val.canConvert<QDBusArgument>()) {
            const QDBusArgument arg = val.value<QDBusArgument>();
            if (arg.currentType() == QDBusArgument::StructureType) {
                arg.beginStructure();
                double r = 0, g = 0, b = 0;
                arg >> r >> g >> b;
                arg.endStructure();
                color = QColor::fromRgbF(std::clamp(r, 0.0, 1.0),
                                         std::clamp(g, 0.0, 1.0),
                                         std::clamp(b, 0.0, 1.0));
            }
        } else if (val.canConvert<QVariantList>()) {
            const auto list = val.toList();
            if (list.size() >= 3) {
                color = QColor::fromRgbF(std::clamp(list[0].toDouble(), 0.0, 1.0),
                                         std::clamp(list[1].toDouble(), 0.0, 1.0),
                                         std::clamp(list[2].toDouble(), 0.0, 1.0));
            }
        }
        if (color.isValid() && color != m_accentColor) {
            m_accentColor = color;
            emit accentColorChanged(m_accentColor);
            emit themeChanged(m_brightness, m_accentColor);
        }
    }
}
#endif

void LinuxTheme::refresh()
{
#if defined(Q_OS_LINUX)
    Brightness newBrightness = Brightness::Unknown;
    QColor newAccent;

    // Follow the precedence established in the reference companion implementation:
    // KDE prefers native KDE config first, then Portal, then GNOME fallback.
    // GNOME prefers Portal, then GSettings, then KDE fallback.
    if (m_desktop == DesktopEnv::Kde) {
        newBrightness = queryKdeBrightness();
        if (newBrightness == Brightness::Unknown)
            newBrightness = queryPortalBrightness();
    } else {
        newBrightness = queryPortalBrightness();
        if (newBrightness == Brightness::Unknown) {
            if (m_desktop == DesktopEnv::Gnome)
                newBrightness = queryGnomeBrightness();
            else {
                newBrightness = queryKdeBrightness();
                if (newBrightness == Brightness::Unknown)
                    newBrightness = queryGnomeBrightness();
            }
        }
    }

    if (newBrightness == Brightness::Unknown)
        newBrightness = queryGtkSettingsBrightness();
    if (newBrightness == Brightness::Unknown)
        newBrightness = queryEnvBrightness();

    switch (m_desktop) {
    case DesktopEnv::Kde:
        newAccent = queryKdeAccent();
        if (!newAccent.isValid())
            newAccent = queryPortalAccent();
        if (!newAccent.isValid())
            newAccent = queryGnomeAccent();
        break;
    case DesktopEnv::Gnome:
        newAccent = queryGnomeAccent();
        if (!newAccent.isValid())
            newAccent = queryPortalAccent();
        if (!newAccent.isValid())
            newAccent = queryKdeAccent();
        break;
    case DesktopEnv::Lxde:
        newAccent = queryLxdeAccent();
        if (!newAccent.isValid())
            newAccent = queryPortalAccent();
        break;
    case DesktopEnv::Other:
        newAccent = queryPortalAccent();
        if (!newAccent.isValid())
            newAccent = queryKdeAccent();
        if (!newAccent.isValid())
            newAccent = queryGnomeAccent();
        break;
    }

    updateValues(newBrightness, newAccent);
#endif
}

void LinuxTheme::updateValues(Brightness newBrightness, const QColor &newAccent)
{
    const bool bChanged = (newBrightness != Brightness::Unknown && newBrightness != m_brightness);
    const bool aChanged = (newAccent.isValid() && newAccent != m_accentColor);

    if (newBrightness != Brightness::Unknown)
        m_brightness = newBrightness;
    if (newAccent.isValid())
        m_accentColor = newAccent;

    if (bChanged)
        emit brightnessChanged(m_brightness);
    if (aChanged)
        emit accentColorChanged(m_accentColor);
    if (bChanged || aChanged)
        emit themeChanged(m_brightness, m_accentColor);
}

} // namespace talos
