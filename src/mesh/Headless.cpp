// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/Headless.h"

#include "core/AppLog.h"
#include "core/Session.h"
#include "core/Store.h"
#include "mesh/MeshService.h"

#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QTimer>
#include <atomic>
#include <csignal>
#include <cstdio>

namespace talos {

using namespace Qt::StringLiterals;

namespace {
std::atomic_bool g_stop{false};
void onSignal(int)
{
    g_stop.store(true);
}
} // namespace

int runHeadless(QCoreApplication &app)
{
    Store store;
    if (!store.hasProfile()) {
        std::fprintf(stderr, "talos: no connection is set up yet. Open the app once and "
                             "connect it to Talon first.\n");
        return 2;
    }
    Session session(&store);
    session.setSnapshotsEnabled(false);
    MeshService mesh(&store, &session);

    QObject::connect(&mesh, &MeshService::ringRequested, &app, [](const QString &message) {
        // No speaker to reach from here; the app rings when it is open.
        AppLog::info("mesh", u"ring requested: %1"_s.arg(message));
    });
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
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    QTimer stop;
    stop.setInterval(300);
    QObject::connect(&stop, &QTimer::timeout, &app, [&app] {
        if (g_stop.load())
            app.quit();
    });
    stop.start();

    AppLog::info("mesh", u"Talos %1 running in the background"_s.arg(QStringLiteral(TALOS_VERSION)));
    session.start();
    return app.exec();
}

} // namespace talos
