// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QColor>
#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

#if defined(TALOS_HAVE_DBUS)
class QDBusVariant;
#endif

namespace talos {

/// Linux desktop theme mode (dark/light) and accent colour detection.
///
/// Qt's standard theme detection (QStyleHints::colorScheme) does not reliably
/// track dark mode or accent colour updates across desktop environments,
/// particularly KDE Plasma, GNOME 42+ XDG portals, and LXDE, nor does it
/// surface system accent colours.
///
/// This service queries the active desktop environment using native config
/// files (kdeglobals), gsettings, and the FreeDesktop Settings portal via D-Bus,
/// and observes live changes through D-Bus signals and inotify file watchers.
class LinuxTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY accentColorChanged)
public:
    enum class Brightness {
        Unknown,
        Light,
        Dark
    };
    Q_ENUM(Brightness)

    enum class DesktopEnv {
        Kde,
        Gnome,
        Lxde,
        Other
    };
    Q_ENUM(DesktopEnv)

    static LinuxTheme *instance();
    static bool isAvailable();

    DesktopEnv desktop() const { return m_desktop; }
    Brightness brightness() const { return m_brightness; }
    bool isDark() const { return m_brightness == Brightness::Dark; }
    QColor accentColor() const { return m_accentColor; }

    /// Fast synchronous initialization before the first frame is rendered.
    void initSync();

    /// Start watching for system theme and accent changes.
    void startMonitoring();

    /// Stop watching for changes.
    void stopMonitoring();

    /// Force an immediate refresh from all available sources.
    void refresh();

signals:
    void themeChanged(Brightness brightness, const QColor &accent);
    void brightnessChanged(Brightness brightness);
    void accentColorChanged(const QColor &accent);

private slots:
    void onConfigFileChanged(const QString &path);
    void checkPeriodic();
#if defined(TALOS_HAVE_DBUS)
    void onPortalSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);
#endif

private:
    explicit LinuxTheme(QObject *parent = nullptr);
    ~LinuxTheme() override;

    static DesktopEnv detectDesktop();
    static QColor parseRgbString(const QString &input);
    static Brightness parseKdeColorScheme(const QString &name);
    static Brightness parseKdeGlobalsColorScheme(const QString &content);
    static QColor parseKdeGlobalsAccent(const QString &content);
    static Brightness parseGnomeColorScheme(const QString &input);
    static QColor parseGnomeAccent(const QString &input);
    static QColor parseLxdeDesktopConf(const QString &content);
    static Brightness parseGtkSettings(const QString &content);

    Brightness queryKdeBrightness();
    QColor queryKdeAccent();
    Brightness queryGnomeBrightness();
    QColor queryGnomeAccent();
    QColor queryLxdeAccent();
    Brightness queryPortalBrightness();
    QColor queryPortalAccent();
    Brightness queryGtkSettingsBrightness();
    Brightness queryEnvBrightness();

    void updateValues(Brightness newBrightness, const QColor &newAccent);
    void ensureWatches();

    DesktopEnv m_desktop = DesktopEnv::Other;
    Brightness m_brightness = Brightness::Unknown;
    QColor m_accentColor;

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_pollTimer = nullptr;
    bool m_monitoring = false;
    bool m_dbusConnected = false;
};

} // namespace talos
