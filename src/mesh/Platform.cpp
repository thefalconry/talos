// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/Platform.h"

#include <QFileInfo>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkInformation>
#include <QSysInfo>
#include <QTimeZone>
#include <QTimer>

#if defined(Q_OS_ANDROID)
#  include <QCoreApplication>
#  include <QJniObject>
#elif defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX // or <windows.h> defines min and max as macros
#  endif
#  include <windows.h>
#elif defined(Q_OS_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <IOKit/ps/IOPSKeys.h>
#  include <IOKit/ps/IOPowerSources.h>
#  include <SystemConfiguration/SystemConfiguration.h>
#  include <sys/sysctl.h>
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#  include <cstdlib>       // getloadavg
#  include <sys/statvfs.h>
#endif
#if defined(Q_OS_MACOS)
#  include <sys/time.h>
#endif

namespace talos::platform {

using namespace Qt::StringLiterals;

QString name()
{
#if defined(Q_OS_ANDROID)
    return u"android"_s;
#elif defined(Q_OS_IOS)
    return u"ios"_s;
#elif defined(Q_OS_MACOS)
    return u"macos"_s;
#elif defined(Q_OS_WIN)
    return u"windows"_s;
#else
    return u"linux"_s;
#endif
}

bool isMobile()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return true;
#else
    return false;
#endif
}

QString deviceName()
{
#if defined(Q_OS_ANDROID)
    const QString maker = QJniObject::getStaticObjectField<jstring>("android/os/Build",
                                                                   "MANUFACTURER").toString();
    const QString model =
        QJniObject::getStaticObjectField<jstring>("android/os/Build", "MODEL").toString();
    const QString label = model.startsWith(maker, Qt::CaseInsensitive)
        ? model : (maker + u' ' + model).trimmed();
    return label.isEmpty() ? u"Android device"_s : label;
#else
#  if defined(Q_OS_MACOS)
    // The name the Mac goes by in Sharing ("Ada's MacBook Air"), not its
    // network name with ".local" on the end.
    if (const CFStringRef given = SCDynamicStoreCopyComputerName(nullptr, nullptr)) {
        const QString name = QString::fromCFString(given).trimmed();
        CFRelease(given);
        if (!name.isEmpty())
            return name;
    }
#  endif
    const QString host = QSysInfo::machineHostName().trimmed();
    if (!host.isEmpty() && host != "localhost"_L1)
        return host;
    return QSysInfo::prettyProductName();
#endif
}

Battery battery()
{
    Battery b;
#if defined(Q_OS_ANDROID)
    // The battery state is a sticky broadcast: registering with no receiver
    // just hands back the latest one.
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject filter("android/content/IntentFilter", "(Ljava/lang/String;)V",
                            QJniObject::fromString(u"android.intent.action.BATTERY_CHANGED"_s)
                                .object<jstring>());
    const QJniObject intent = context.callObjectMethod(
        "registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)"
        "Landroid/content/Intent;",
        nullptr, filter.object());
    if (intent.isValid()) {
        const auto extra = [&intent](const QString &key) {
            return intent.callMethod<jint>("getIntExtra", "(Ljava/lang/String;I)I",
                                           QJniObject::fromString(key).object<jstring>(), -1);
        };
        const int level = extra(u"level"_s);
        const int scale = extra(u"scale"_s);
        const int status = extra(u"status"_s);
        if (level >= 0 && scale > 0)
            b.percent = qRound(100.0 * level / scale);
        if (status > 0)
            b.charging = (status == 2 || status == 5) ? 1 : 0; // CHARGING, FULL
    }
#elif defined(Q_OS_WIN)
    SYSTEM_POWER_STATUS s;
    if (GetSystemPowerStatus(&s)) {
        if (s.BatteryLifePercent <= 100 && !(s.BatteryFlag & 128)) // 128: no battery
            b.percent = s.BatteryLifePercent;
        if (s.ACLineStatus <= 1 && b.percent >= 0)
            b.charging = s.ACLineStatus;
    }
#elif defined(Q_OS_MACOS)
    const CFTypeRef blob = IOPSCopyPowerSourcesInfo();
    const CFArrayRef sources = blob ? IOPSCopyPowerSourcesList(blob) : nullptr;
    for (CFIndex i = 0; sources && i < CFArrayGetCount(sources); ++i) {
        const CFDictionaryRef source =
            IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(sources, i));
        if (!source)
            continue;
        // The Mac's own battery, not a UPS or a paired keyboard.
        const auto type = static_cast<CFStringRef>(
            CFDictionaryGetValue(source, CFSTR(kIOPSTypeKey)));
        if (!type || !CFEqual(type, CFSTR(kIOPSInternalBatteryType)))
            continue;
        const auto number = [source](CFStringRef key) {
            int value = -1;
            const auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(source, key));
            if (n)
                CFNumberGetValue(n, kCFNumberIntType, &value);
            return value;
        };
        const int now = number(CFSTR(kIOPSCurrentCapacityKey));
        const int full = number(CFSTR(kIOPSMaxCapacityKey));
        if (now < 0 || full <= 0)
            continue;
        b.percent = qBound(0, qRound(100.0 * now / full), 100);
        // On mains counts as charging, as "Full" does on Linux: macOS holds
        // a battery below full for long stretches while plugged in.
        const auto state = static_cast<CFStringRef>(
            CFDictionaryGetValue(source, CFSTR(kIOPSPowerSourceStateKey)));
        if (state)
            b.charging = CFEqual(state, CFSTR(kIOPSACPowerValue)) ? 1 : 0;
        break;
    }
    if (sources)
        CFRelease(sources);
    if (blob)
        CFRelease(blob);
#elif defined(Q_OS_LINUX)
    const QDir supplies(u"/sys/class/power_supply"_s);
    const auto read = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed()
                                           : QString();
    };
    for (const QString &entry : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = supplies.filePath(entry);
        // "Battery" also covers mice and headsets; those are scoped Device.
        if (read(base + "/type"_L1) != "Battery"_L1 || read(base + "/scope"_L1) == "Device"_L1)
            continue;
        bool ok = false;
        const int capacity = read(base + "/capacity"_L1).toInt(&ok);
        if (!ok)
            continue;
        b.percent = qBound(0, capacity, 100);
        const QString status = read(base + "/status"_L1);
        b.charging = (status == "Charging"_L1 || status == "Full"_L1) ? 1 : 0;
        break;
    }
#endif
    return b;
}

QString formatBytes(quint64 bytes)
{
    if (bytes < 1024)
        return u"%1 B"_s.arg(bytes);
    quint64 div = 1024;
    int exp = 0;
    for (quint64 n = bytes / 1024; n >= 1024; n /= 1024) {
        div *= 1024;
        ++exp;
    }
    return u"%1 %2B"_s.arg(double(bytes) / double(div), 0, 'f', 1)
        .arg(QChar::fromLatin1("KMGTPE"[exp]));
}

QString formatUptime(qint64 seconds)
{
    if (seconds < 0)
        seconds = 0;
    const qint64 h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    if (h > 0)
        return u"%1h%2m%3s"_s.arg(h).arg(m).arg(s);
    if (m > 0)
        return u"%1m%2s"_s.arg(m).arg(s);
    return u"%1s"_s.arg(s);
}

QString formatFreeOf(quint64 free, quint64 total, const QString &where)
{
    if (total == 0 || free > total)
        return {};
    QString text = u"%1 free of %2"_s.arg(formatBytes(free), formatBytes(total));
    if (!where.isEmpty())
        text += u" on "_s + where;
    return text;
}

namespace {

void putIfSet(QVariantMap &info, const QString &key, const QString &value)
{
    if (!value.isEmpty())
        info[key] = value;
}

#if defined(Q_OS_MACOS)
/// An integer sysctl of either width; these names vary between kernels.
bool sysctlUint(const char *name, quint64 &out)
{
    quint64 value = 0;
    size_t size = sizeof value;
    if (sysctlbyname(name, &value, &size, nullptr, 0) != 0 || size == 0 || size > sizeof value)
        return false;
    out = value; // little-endian: a 4-byte answer fills the low half
    return true;
}
#endif

/// Uptime, load, memory and disk, each left out when its probe fails. Native
/// calls only: no subprocesses, so a status request stays cheap.
void addHostTelemetry(QVariantMap &info)
{
#if defined(Q_OS_WIN)
    // Ported from talon PR 1376 by Dylan Neve. Windows has no load average.
    info[u"uptime"_s] = formatUptime(qint64(GetTickCount64() / 1000));
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof mem;
    if (GlobalMemoryStatusEx(&mem))
        putIfSet(info, u"memory"_s, formatFreeOf(mem.ullAvailPhys, mem.ullTotalPhys));
    QString drive = qEnvironmentVariable("SystemDrive");
    if (drive.isEmpty())
        drive = u"C:"_s;
    const QString root = drive + u'\\';
    ULARGE_INTEGER freeToUser{}, total{};
    if (GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(root.utf16()), &freeToUser, &total, nullptr))
        putIfSet(info, u"disk"_s, formatFreeOf(freeToUser.QuadPart, total.QuadPart, drive));
#elif !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#  if defined(Q_OS_MACOS)
    timeval boot{};
    size_t size = sizeof boot;
    if (sysctlbyname("kern.boottime", &boot, &size, nullptr, 0) == 0 && boot.tv_sec > 0)
        info[u"uptime"_s] = formatUptime(QDateTime::currentSecsSinceEpoch() - boot.tv_sec);
    // Free, purgeable and speculative pages, as the node counts them: it
    // leaves out inactive file cache, so it reads lower than Linux's figure.
    quint64 total = 0, page = 0, free = 0, purgeable = 0, speculative = 0;
    if (sysctlUint("hw.memsize", total) && sysctlUint("hw.pagesize", page)
        && sysctlUint("vm.page_free_count", free)
        && sysctlUint("vm.page_purgeable_count", purgeable)
        && sysctlUint("vm.page_speculative_count", speculative))
        putIfSet(info, u"memory"_s, formatFreeOf((free + purgeable + speculative) * page, total));
#  else
    QFile uptime(u"/proc/uptime"_s);
    if (uptime.open(QIODevice::ReadOnly)) {
        bool ok = false;
        const double secs = QString::fromLatin1(uptime.readAll()).section(u' ', 0, 0).toDouble(&ok);
        if (ok)
            info[u"uptime"_s] = formatUptime(qint64(secs));
    }
    QFile meminfo(u"/proc/meminfo"_s);
    if (meminfo.open(QIODevice::ReadOnly)) {
        quint64 total = 0, available = 0;
        const auto lines = QString::fromLatin1(meminfo.readAll()).split(u'\n');
        for (const QString &line : lines) {
            const QStringList f = line.simplified().split(u' ');
            if (f.size() < 2)
                continue;
            if (f[0] == "MemTotal:"_L1)
                total = f[1].toULongLong() * 1024;
            else if (f[0] == "MemAvailable:"_L1)
                available = f[1].toULongLong() * 1024;
        }
        if (available > 0)
            putIfSet(info, u"memory"_s, formatFreeOf(available, total));
    }
#  endif
    double load[3];
    if (getloadavg(load, 3) == 3)
        info[u"load"_s] = u"%1 %2 %3"_s.arg(load[0], 0, 'f', 2).arg(load[1], 0, 'f', 2)
                              .arg(load[2], 0, 'f', 2);
    struct statvfs fs {};
    if (statvfs("/", &fs) == 0)
        putIfSet(info, u"disk"_s,
                 formatFreeOf(quint64(fs.f_bavail) * fs.f_frsize,
                              quint64(fs.f_blocks) * fs.f_frsize, u"/"_s));
#else
    Q_UNUSED(info);
#endif
}

} // namespace

QVariantMap systemInfo()
{
    QVariantMap info;
    info[u"os"_s] = QSysInfo::prettyProductName();
    info[u"kernel"_s] = QSysInfo::kernelType() + u' ' + QSysInfo::kernelVersion();
    info[u"arch"_s] = QSysInfo::currentCpuArchitecture();
    info[u"locale"_s] = QLocale::system().name();
    const QDateTime now = QDateTime::currentDateTime();
    const int offset = now.offsetFromUtc();
    info[u"timezone"_s] = u"%1 (UTC%2%3:%4)"_s
        .arg(QString::fromLatin1(now.timeZone().id()))
        .arg(offset < 0 ? u'-' : u'+')
        .arg(qAbs(offset) / 3600, 2, 10, QLatin1Char('0'))
        .arg((qAbs(offset) % 3600) / 60, 2, 10, QLatin1Char('0'));
#if defined(Q_OS_ANDROID)
    info[u"hardware"_s] = deviceName();
#elif defined(Q_OS_MACOS)
    char model[64] = {};
    size_t size = sizeof model - 1;
    if (sysctlbyname("hw.model", model, &size, nullptr, 0) == 0 && model[0])
        info[u"hardware"_s] = QString::fromLatin1(model);
#endif
    if (QNetworkInformation *net = QNetworkInformation::instance()) {
        if (net->reachability() == QNetworkInformation::Reachability::Disconnected) {
            info[u"network"_s] = u"offline"_s;
        } else {
            switch (net->transportMedium()) {
            case QNetworkInformation::TransportMedium::Ethernet: info[u"network"_s] = u"ethernet"_s; break;
            case QNetworkInformation::TransportMedium::WiFi: info[u"network"_s] = u"wifi"_s; break;
            case QNetworkInformation::TransportMedium::Cellular: info[u"network"_s] = u"mobile"_s; break;
            case QNetworkInformation::TransportMedium::Bluetooth: info[u"network"_s] = u"bluetooth"_s; break;
            default: info[u"network"_s] = u"online"_s; break;
            }
        }
    }
    addHostTelemetry(info);
    return info;
}

bool isSandboxed()
{
    return qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists(u"/.flatpak-info"_s)
        || qEnvironmentVariableIsSet("SNAP");
}

bool moveToBackground()
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        activity.callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true));
    });
    return true;
#else
    return false;
#endif
}

void setSecureWindow(bool on)
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on] {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        const QJniObject window =
            activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (!window.isValid())
            return;
        constexpr jint kFlagSecure = 0x2000; // WindowManager.LayoutParams.FLAG_SECURE
        if (on)
            window.callMethod<void>("addFlags", "(I)V", kFlagSecure);
        else
            window.callMethod<void>("clearFlags", "(I)V", kFlagSecure);
    });
#else
    Q_UNUSED(on);
#endif
}

void setKeepAlive(bool on)
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>("dev/talos/client/MeshHost", "set",
                                           "(Landroid/content/Context;Z)V", context.object(),
                                           jboolean(on));
    });
#else
    Q_UNUSED(on);
#endif
}

bool batteryUnrestricted()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    return QJniObject::callStaticMethod<jboolean>("dev/talos/client/MeshHost", "unrestricted",
                                                  "(Landroid/content/Context;)Z",
                                                  context.object());
#else
    return true;
#endif
}

void askBatteryUnrestricted()
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>("dev/talos/client/MeshHost", "askUnrestricted",
                                           "(Landroid/content/Context;)V", context.object());
    });
#endif
}

QString takeShared()
{
#if defined(Q_OS_ANDROID)
    const QJniObject share = QJniObject::callStaticObjectMethod(
        "dev/talos/client/TalosActivity", "takeShared", "()Ljava/lang/String;");
    return share.isValid() ? share.toString() : QString();
#else
    return {};
#endif
}

void setSystemBars(bool light, unsigned int argb)
{
#if defined(Q_OS_ANDROID)
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>("dev/talos/client/TalosActivity", "setBars",
                                       "(Landroid/app/Activity;ZI)V", activity.object(),
                                       jboolean(light), jint(argb));
#else
    Q_UNUSED(light);
    Q_UNUSED(argb);
#endif
}

QString wallpaperColors()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject colors = QJniObject::callStaticObjectMethod(
        "dev/talos/client/TalosActivity", "wallpaperColors",
        "(Landroid/content/Context;)Ljava/lang/String;", context.object());
    return colors.isValid() ? colors.toString() : QString();
#else
    return {};
#endif
}

void haptic(int kind)
{
#if defined(Q_OS_ANDROID)
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>("dev/talos/client/TalosActivity", "haptic",
                                       "(Landroid/app/Activity;I)V", activity.object(), jint(kind));
#else
    Q_UNUSED(kind);
#endif
}

bool localNetworkAllowed()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    return QJniObject::callStaticMethod<jboolean>(
        "dev/talos/client/TalosActivity", "localNetworkAllowed", "(Landroid/content/Context;)Z",
        context.object());
#else
    return true;
#endif
}

void askForLocalNetwork()
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>("dev/talos/client/TalosActivity", "askForLocalNetwork",
                                           "(Landroid/app/Activity;)V", activity.object());
    });
#endif
}

bool allFilesAllowed()
{
#if defined(Q_OS_ANDROID)
    return QJniObject::callStaticMethod<jboolean>("dev/talos/client/TalosActivity",
                                                  "allFilesAllowed", "()Z");
#else
    return true;
#endif
}

void askForAllFiles()
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>("dev/talos/client/TalosActivity", "askForAllFiles",
                                           "(Landroid/content/Context;)V", context.object());
    });
#endif
}

void holdAwake(bool hold)
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>("dev/talos/client/MeshHost", "holdAwake",
                                       "(Landroid/content/Context;Z)V", context.object(),
                                       jboolean(hold));
#else
    Q_UNUSED(hold);
#endif
}

QString installUpdate(const QString &path)
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject error = QJniObject::callStaticObjectMethod(
        "dev/talos/client/ApkInstaller", "installUpdate",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;", context.object(),
        QJniObject::fromString(path).object<jstring>());
    return error.isValid() ? error.toString() : QString();
#else
    Q_UNUSED(path);
    return u"Installing packages is only possible on Android."_s;
#endif
}

void askForNotifications()
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>("dev/talos/client/TalosActivity", "askForNotifications",
                                           "(Landroid/app/Activity;)V", activity.object());
    });
#endif
}

namespace {
QString uiMarker(const QString &dataDir)
{
    return QDir(dataDir).filePath(u"ui.active"_s);
}
} // namespace

void markUiActive(const QString &dataDir, bool active)
{
    if (!active) {
        QFile::remove(uiMarker(dataDir));
        return;
    }
    QFile marker(uiMarker(dataDir));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        marker.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
}

bool uiActive(const QString &dataDir)
{
    // Refreshed every few seconds while the window is in front; a stale one
    // is a window that went away without saying so.
    const QFileInfo marker(uiMarker(dataDir));
    return marker.exists() && marker.lastModified().secsTo(QDateTime::currentDateTime()) < 30;
}

Elevation elevation(bool wait)
{
    Elevation e;
    e.state = u"absent"_s;
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject status = QJniObject::callStaticObjectMethod(
        "dev/talos/client/Elevated", "status",
        "(Landroid/content/Context;Z)Ljava/lang/String;", context.object(), jboolean(wait));
    if (status.isValid()) {
        const QStringList parts = status.toString().split(u' ');
        e.state = parts.value(0);
        e.uid = parts.size() > 1 ? parts.at(1).toInt() : -1;
    }
#else
    Q_UNUSED(wait)
#endif
    return e;
}

void askElevation()
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>("dev/talos/client/Elevated", "ask",
                                           "(Landroid/content/Context;)V", context.object());
    });
#endif
}

ElevatedRun runElevated(const QString &cmd, const QString &cwd, int timeoutMs,
                        const QString &input)
{
    ElevatedRun r;
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject answer = QJniObject::callStaticObjectMethod(
        "dev/talos/client/Elevated", "run",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)"
        "Ljava/lang/String;",
        context.object(), QJniObject::fromString(cmd).object<jstring>(),
        QJniObject::fromString(cwd).object<jstring>(), jint(timeoutMs),
        QJniObject::fromString(input).object<jstring>());
    const QJsonObject o = answer.isValid()
        ? QJsonDocument::fromJson(answer.toString().toUtf8()).object() : QJsonObject();
    r.ran = o["ran"_L1].toBool();
    r.error = o["error"_L1].toString();
    r.out = o["stdout"_L1].toString();
    r.err = o["stderr"_L1].toString();
    r.code = o["exitCode"_L1].toInt(-1);
    r.timedOut = o["timedOut"_L1].toBool();
    r.uid = o["uid"_L1].toInt(-1);
#else
    Q_UNUSED(cmd)
    Q_UNUSED(cwd)
    Q_UNUSED(timeoutMs)
    Q_UNUSED(input)
    r.error = u"Shizuku is an Android service."_s;
#endif
    return r;
}

QString updateProblem(const QString &path)
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject problem = QJniObject::callStaticObjectMethod(
        "dev/talos/client/ApkInstaller", "updateProblem",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;", context.object(),
        QJniObject::fromString(path).object<jstring>());
    return problem.isValid() ? problem.toString() : QString();
#else
    Q_UNUSED(path)
    return {};
#endif
}

QString launchComponent()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject name = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
    return name.isValid() ? name.toString() + u"/dev.talos.client.TalosActivity"_s : QString();
#else
    return {};
#endif
}

QString installPackage(const QString &path)
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject error = QJniObject::callStaticObjectMethod(
        "dev/talos/client/ApkInstaller", "install",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;", context.object(),
        QJniObject::fromString(path).object<jstring>());
    return error.isValid() ? error.toString() : QString();
#else
    Q_UNUSED(path);
    return u"Installing packages is only possible on Android."_s;
#endif
}

QString publishDownload(const QString &file, const QString &name)
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject uri = QJniObject::callStaticObjectMethod(
        "dev/talos/client/Downloads", "publish",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        context.object(), QJniObject::fromString(file).object<jstring>(),
        QJniObject::fromString(name).object<jstring>());
    return uri.isValid() ? uri.toString() : QString();
#else
    Q_UNUSED(file);
    Q_UNUSED(name);
    return {};
#endif
}

bool notifyNative(const QString &title, const QString &body, const QString &chatId)
{
#if defined(Q_OS_ANDROID)
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([title, body, chatId] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "dev/talos/client/Notify", "post",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
            context.object(), QJniObject::fromString(title).object<jstring>(),
            QJniObject::fromString(body).object<jstring>(),
            QJniObject::fromString(chatId).object<jstring>());
    });
    return true;
#else
    Q_UNUSED(title);
    Q_UNUSED(body);
    Q_UNUSED(chatId);
    return false;
#endif
}

void cancelChatNotification(const QString &chatId)
{
#if defined(Q_OS_ANDROID)
    if (chatId.isEmpty())
        return;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([chatId] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        QJniObject::callStaticMethod<void>(
            "dev/talos/client/Notify", "cancel", "(Landroid/content/Context;Ljava/lang/String;)V",
            context.object(), QJniObject::fromString(chatId).object<jstring>());
    });
#else
    Q_UNUSED(chatId);
#endif
}

QString takeOpenChat()
{
#if defined(Q_OS_ANDROID)
    const QJniObject chat = QJniObject::callStaticObjectMethod(
        "dev/talos/client/TalosActivity", "takeOpenChat", "()Ljava/lang/String;");
    return chat.isValid() ? chat.toString() : QString();
#else
    return {};
#endif
}

bool ringNative()
{
#if defined(Q_OS_ANDROID)
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    // The ringtone the phone itself would use: loud, and familiar to its owner.
    QJniObject uri = QJniObject::callStaticObjectMethod(
        "android/media/RingtoneManager", "getDefaultUri", "(I)Landroid/net/Uri;", jint(1));
    if (!uri.isValid())
        return false;
    const QJniObject tone = QJniObject::callStaticObjectMethod(
        "android/media/RingtoneManager", "getRingtone",
        "(Landroid/content/Context;Landroid/net/Uri;)Landroid/media/Ringtone;", context.object(),
        uri.object());
    if (!tone.isValid())
        return false;
    // As an alarm: it is meant to be heard on a phone left on silent, and
    // from Android 17 it is the one sound a background service may make.
    QJniObject attributes("android/media/AudioAttributes$Builder");
    attributes.callObjectMethod("setUsage", "(I)Landroid/media/AudioAttributes$Builder;",
                                jint(4)); // AudioAttributes.USAGE_ALARM
    const QJniObject alarm =
        attributes.callObjectMethod("build", "()Landroid/media/AudioAttributes;");
    tone.callMethod<void>("setAudioAttributes", "(Landroid/media/AudioAttributes;)V",
                          alarm.object());
    tone.callMethod<void>("play");
    QTimer::singleShot(8000, qApp, [tone] { tone.callMethod<void>("stop"); });
    return true;
#else
    return false;
#endif
}

} // namespace talos::platform
