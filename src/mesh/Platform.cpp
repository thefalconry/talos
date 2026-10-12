// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/Platform.h"

#include <QFileInfo>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QNetworkInformation>
#include <QSysInfo>
#include <QTimeZone>
#include <QTimer>

#if defined(Q_OS_ANDROID)
#  include <QCoreApplication>
#  include <QJniObject>
#elif defined(Q_OS_WIN)
#  include <windows.h>
#elif defined(Q_OS_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <IOKit/ps/IOPSKeys.h>
#  include <IOKit/ps/IOPowerSources.h>
#  include <SystemConfiguration/SystemConfiguration.h>
#  include <sys/sysctl.h>
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

bool notifyNative(const QString &title, const QString &body)
{
#if defined(Q_OS_ANDROID)
    static int nextId = 1;
    const int id = nextId++;
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([title, body, id] {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        const QJniObject channelId = QJniObject::fromString(u"talon.messages"_s);
        const QJniObject manager = context.callObjectMethod(
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            QJniObject::fromString(u"notification"_s).object<jstring>());
        if (!manager.isValid())
            return;
        // Creating a channel that already exists is a no-op.
        const QJniObject channel(
            "android/app/NotificationChannel", "(Ljava/lang/String;Ljava/lang/CharSequence;I)V",
            channelId.object<jstring>(), QJniObject::fromString(u"Messages"_s).object<jstring>(),
            jint(3)); // IMPORTANCE_DEFAULT
        manager.callMethod<void>("createNotificationChannel",
                                 "(Landroid/app/NotificationChannel;)V", channel.object());

        QJniObject builder("android/app/Notification$Builder",
                           "(Landroid/content/Context;Ljava/lang/String;)V", context.object(),
                           channelId.object<jstring>());
        const jint icon = QJniObject::getStaticField<jint>("android/R$drawable",
                                                           "stat_notify_chat");
        builder.callObjectMethod("setSmallIcon", "(I)Landroid/app/Notification$Builder;", icon);
        builder.callObjectMethod("setContentTitle",
                                 "(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;",
                                 QJniObject::fromString(title).object<jstring>());
        builder.callObjectMethod("setContentText",
                                 "(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;",
                                 QJniObject::fromString(body).object<jstring>());
        builder.callObjectMethod("setAutoCancel", "(Z)Landroid/app/Notification$Builder;",
                                 jboolean(true));
        // Tapping it opens the app.
        const QJniObject packages =
            context.callObjectMethod("getPackageManager", "()Landroid/content/pm/PackageManager;");
        const QJniObject package = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
        const QJniObject open = packages.callObjectMethod(
            "getLaunchIntentForPackage", "(Ljava/lang/String;)Landroid/content/Intent;",
            package.object<jstring>());
        if (open.isValid()) {
            constexpr jint kImmutableUpdate = 0x04000000 | 0x08000000;
            const QJniObject tap = QJniObject::callStaticObjectMethod(
                "android/app/PendingIntent", "getActivity",
                "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;",
                context.object(), jint(0), open.object(), kImmutableUpdate);
            builder.callObjectMethod(
                "setContentIntent",
                "(Landroid/app/PendingIntent;)Landroid/app/Notification$Builder;", tap.object());
        }
        const QJniObject notification =
            builder.callObjectMethod("build", "()Landroid/app/Notification;");
        manager.callMethod<void>("notify", "(ILandroid/app/Notification;)V", jint(id),
                                 notification.object());
    });
    return true;
#else
    Q_UNUSED(title);
    Q_UNUSED(body);
    return false;
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
    tone.callMethod<void>("play");
    QTimer::singleShot(8000, qApp, [tone] { tone.callMethod<void>("stop"); });
    return true;
#else
    return false;
#endif
}

} // namespace talos::platform
