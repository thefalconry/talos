// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/Headless.h"

#include "core/AppLog.h"
#include "core/Session.h"
#include "core/Store.h"
#include "mesh/MeshService.h"
#include "mesh/Platform.h"

#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QSocketNotifier>
#include <QTimer>
#include <atomic>
#include <csignal>
#include <cstdio>

#if defined(Q_OS_UNIX)
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace talos {

using namespace Qt::StringLiterals;

namespace {
std::atomic_bool g_stop{false};
#if defined(Q_OS_UNIX)
// A pipe the signal handler writes one byte into: the event loop sleeps on
// its other end, so a service that may run for months is not woken three
// times a second only to ask whether it has been told to stop.
int g_wake[2] = {-1, -1};
#endif
void onSignal(int)
{
    g_stop.store(true);
#if defined(Q_OS_UNIX)
    if (g_wake[1] >= 0) {
        const char byte = 1;
        [[maybe_unused]] const auto written = ::write(g_wake[1], &byte, 1);
    }
#endif
}
} // namespace

int runHeadless(QCoreApplication &app)
{
    Store store;
#if !defined(Q_OS_ANDROID)
    if (!store.hasProfile()) {
        std::fprintf(stderr, "talos: no connection is set up yet. Open the app once and "
                             "connect it to Talon first.\n");
        return 2;
    }
#endif
    Session session(&store);
    session.setSnapshotsEnabled(false);
    MeshService mesh(&store, &session);

    QObject::connect(&mesh, &MeshService::ringRequested, &app, [](const QString &message) {
        // A phone's own ringtone can be played from here; on a desktop
        // there is no speaker to reach, and the app rings when it is open.
        if (platform::ringNative()) {
            platform::notifyNative(u"Talon is looking for this device"_s,
                                   message.isEmpty() ? u"Someone asked Talon to ring it."_s
                                                     : message);
        }
        AppLog::info("mesh", u"ring requested: %1"_s.arg(message));
    });
#if defined(Q_OS_ANDROID)
    // With the window closed this process is the one that hears a reply
    // arrive, so it is the one that says so.
    QObject::connect(&session, &Session::messageArrived, &app,
                     [&store, &session](const QString &, const QString &title, const QString &text,
                                        bool fromAssistant) {
        if (!fromAssistant || !store.prefBool(u"notifications"_s)
            || platform::uiActive(store.dataDir()))
            return;
        // The app lock's state lives in the window's process; with a lock
        // set, never show what a reply says.
        const bool preview = store.prefBool(u"notifyPreview"_s) && !store.lockConfigured();
        QString body = text.simplified();
        if (body.size() > 180)
            body = body.left(179) + u'…';
        platform::notifyNative(title.isEmpty() ? session.botName() : title,
                               preview && !body.isEmpty() ? body : u"New message"_s);
    });
#endif
    QObject::connect(&mesh, &MeshService::restartRequested, &app, [&app] {
        // The service manager starts the new build.
        AppLog::info("mesh", u"restarting into the updated build"_s);
        app.exit(0);
    });
    QObject::connect(&mesh, &MeshService::changed, &app, [&mesh] {
        static QString last;
        if (mesh.state() != last) {
            last = mesh.state();
            AppLog::info("mesh", last);
        }
    });

    // The app owns the settings; follow what it writes.
    QFileSystemWatcher watcher;
    watcher.addPath(store.configDir());
    QTimer reload;
    reload.setSingleShot(true);
    reload.setInterval(500);
    QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &reload,
                     qOverload<>(&QTimer::start));
    QObject::connect(&reload, &QTimer::timeout, &app, [&store, &session] {
        const QString before = store.activeProfileId();
        store.reloadPrefs();
        if (store.activeProfileId() != before)
            session.start();
    });

    // SIGTERM from the service manager: leave through the event loop so the
    // stream is closed and the mesh lock released.
    QTimer stop;
    QObject::connect(&stop, &QTimer::timeout, &app, [&app] {
        if (g_stop.load())
            app.quit();
    });
#if defined(Q_OS_UNIX)
    if (::pipe(g_wake) == 0) {
        for (const int fd : g_wake)
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        auto *woken = new QSocketNotifier(g_wake[0], QSocketNotifier::Read, &app);
        QObject::connect(woken, &QSocketNotifier::activated, &app, [&app] { app.quit(); });
    } else {
        g_wake[0] = g_wake[1] = -1;
    }
#endif
    // Without the pipe (or where there is none), look now and then.
#if defined(Q_OS_UNIX)
    if (g_wake[0] < 0)
#endif
    {
        stop.setInterval(300);
        stop.start();
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    AppLog::info("mesh", u"Talos %1 running in the background"_s.arg(QStringLiteral(TALOS_VERSION)));
    session.start();
    return app.exec();
}

} // namespace talos
