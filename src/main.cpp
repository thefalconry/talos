// SPDX-License-Identifier: GPL-2.0-only
#include "app/App.h"
#include "app/CodeHighlighter.h"
#include "app/FilePicker.h"
#include "app/FlowChartItem.h"
#include "app/IconItem.h"
#include "app/NoNetwork.h"
#include "core/Store.h"
#include "mesh/Headless.h"
#include "mesh/ServiceInstall.h"

#include <QApplication>
#if defined(Q_OS_ANDROID)
#  include <QtCore/private/qandroidextras_p.h>
#endif
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace {

/// One instance per user and data directory. A second launch (clicking a
/// talon:// link starts one) hands its link to the first and exits.
QString instanceName()
{
    const QByteArray scope = qgetenv("USER") + qgetenv("USERNAME") + '\n'
        + qgetenv("TALOS_HOME");
    return u"talos-"_s
        + QString::fromLatin1(QCryptographicHash::hash(scope, QCryptographicHash::Sha1)
                                  .toHex().left(12));
}

bool handOver(const QString &link)
{
    QLocalSocket socket;
    socket.connectToServer(instanceName());
    if (!socket.waitForConnected(300))
        return false;
    socket.write(link.toUtf8() + '\n');
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

/// Drive the UI from a JSON script and save screenshots: a development aid
/// for checking layouts and producing documentation images.
///   [{"wait": 800, "eval": "root.openSettings()", "shot": "settings.png"}]
/// A step may also carry "click": [x, y] (a left click at that point of the
/// window) or "key": "back" (the Android back key: a key event, then a close
/// request if nothing took it). Both run before the step's "eval". A step's
/// "notify": "text" posts one real notification, which a scripted run
/// otherwise never does.
void runScript(const QString &file, QQmlApplicationEngine *engine, talos::App *app)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("cannot read script %s", qPrintable(file));
        QCoreApplication::exit(2);
        return;
    }
    auto steps = std::make_shared<QJsonArray>(QJsonDocument::fromJson(f.readAll()).array());
    auto index = std::make_shared<int>(0);
    auto next = std::make_shared<std::function<void()>>();
    *next = [=] {
        if (*index >= steps->size()) {
            QCoreApplication::quit();
            return;
        }
        const QJsonObject step = steps->at((*index)++).toObject();
        QTimer::singleShot(step["wait"_L1].toInt(300), app, [=] {
            auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
            const QJsonArray click = step["click"_L1].toArray();
            if (window && click.size() == 2) {
                const QPointF at(click[0].toDouble(), click[1].toDouble());
                QMouseEvent press(QEvent::MouseButtonPress, at, window->mapToGlobal(at),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, at, window->mapToGlobal(at),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &release);
            }
            if (window && step["key"_L1].toString() == "back"_L1) {
                QKeyEvent press(QEvent::KeyPress, Qt::Key_Back, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &press);
                QKeyEvent release(QEvent::KeyRelease, Qt::Key_Back, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &release);
                if (!press.isAccepted() && !release.isAccepted())
                    window->close();
            }
            const QString notice = step["notify"_L1].toString();
            if (!notice.isEmpty()) {
                app->desktop()->setMuted(false);
                app->desktop()->notify(u"Talos"_s, notice);
                app->desktop()->setMuted(true);
            }
            const QString code = step["eval"_L1].toString();
            if (!code.isEmpty()) {
                QObject *root = engine->rootObjects().first();
                // The root object's own context, so the script sees what
                // Main.qml imports (App, Theme) and its ids.
                QQmlExpression expr(qmlContext(root), root, code);
                expr.evaluate();
                if (expr.hasError())
                    qWarning("script: %s", qPrintable(expr.error().toString()));
            }
            const QString shot = step["shot"_L1].toString();
            if (shot.isEmpty()) {
                (*next)();
                return;
            }
            // Let the frame that the expression caused actually render.
            QTimer::singleShot(step["settle"_L1].toInt(350), app, [=] {
                if (!app->grab(shot))
                    qWarning("script: could not save %s", qPrintable(shot));
                (*next)();
            });
        });
    };
    (*next)();
}

/// `talos --headless` and `talos service …` need no display, and the
/// service has none: they run on a plain event loop.
int runWithoutDisplay(int argc, char *argv[], bool serviceCommand)
{
#if defined(Q_OS_ANDROID)
    // Started by MeshHost: the same event loop, tied to the service's life.
    QAndroidService core(argc, argv);
#else
    QCoreApplication core(argc, argv);
#endif
    QCoreApplication::setOrganizationName(u"Talos"_s);
    QCoreApplication::setApplicationName(u"talos"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(TALOS_VERSION));
    if (!serviceCommand)
        return talos::runHeadless(core);

    const QString verb = core.arguments().value(2);
    QTextStream out(stdout);
    if (!talos::service::supported()) {
        out << "talos: a background service isn't available on this system.\n";
        return 1;
    }
    if (verb == "status"_L1) {
        out << "service: " << talos::service::state() << "\n";
#if !defined(Q_OS_MACOS)
        out << "runs after sign-out: " << (talos::service::lingering() ? "yes" : "no") << "\n";
#endif
        return 0;
    }
    talos::Store store;
    QString error;
    if (verb == "install"_L1) {
        if (!store.hasProfile()) {
            out << "talos: connect the app to Talon first; the service uses that connection.\n";
            return 2;
        }
        if (!talos::service::enable(&store, &error)) {
            out << "talos: " << error << "\n";
            return 1;
        }
        out << "Installed and started " << talos::service::unitPath() << "\n";
#if defined(Q_OS_MACOS)
        out << "It runs whenever you are signed in to this Mac.\n";
#else
        if (!talos::service::lingering())
            out << "It stops when you sign out. To keep it running across sign-outs and "
                   "reboots: loginctl enable-linger\n";
#endif
        return 0;
    }
    if (verb == "uninstall"_L1) {
        if (!talos::service::disable(&store, &error)) {
            out << "talos: " << error << "\n";
            return 1;
        }
        out << "Removed the background service.\n";
        return 0;
    }
    out << "usage: talos service install|uninstall|status\n";
    return 64;
}

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--headless") == 0)
            return runWithoutDisplay(argc, argv, false);
    }
    if (argc > 1 && qstrcmp(argv[1], "service") == 0)
        return runWithoutDisplay(argc, argv, true);

    QApplication qapp(argc, argv);
    QCoreApplication::setOrganizationName(u"Talos"_s);
    QCoreApplication::setApplicationName(u"talos"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(TALOS_VERSION));
    QGuiApplication::setApplicationDisplayName(u"Talos"_s);
    QGuiApplication::setDesktopFileName(u"dev.talos.client"_s);
    QGuiApplication::setWindowIcon(talos::Desktop::appIcon());
    // Closing the window may only hide it (tray); quitting is explicit.
    QGuiApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Talos, a desktop and mobile client for the Talon agent"_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption hidden(u"hidden"_s, u"Start in the tray without showing the window."_s);
    const QCommandLineOption headless(
        u"headless"_s, u"Run without a window: the connection and the mesh only."_s);
    const QCommandLineOption script(u"script"_s,
                                    u"Run a UI script (JSON) and exit; a development aid."_s,
                                    u"file"_s);
    parser.addOption(hidden);
    parser.addOption(headless);
    const QCommandLineOption quick(u"quick"_s,
                                   u"Open the small quick-ask window (bind this to a key)."_s);
    parser.addOption(quick);
    parser.addOption(script);
    parser.addPositionalArgument(u"link"_s, u"A talon://pair link to open."_s, u"[link]"_s);
    parser.process(qapp);
    const QString link = parser.isSet(quick) ? u"talos:quick"_s
                                             : parser.positionalArguments().value(0);

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QLocalServer instance;
    if (!parser.isSet(script)) {
        if (handOver(link))
            return 0;
        QLocalServer::removeServer(instanceName());
        instance.setSocketOptions(QLocalServer::UserAccessOption);
        instance.listen(instanceName());
    }
#endif

    QQuickStyle::setStyle(u"Basic"_s);
    talos::App app;
    qmlRegisterSingletonInstance("Talos.Native", 1, 0, "App", &app);
    qmlRegisterType<talos::CodeHighlighter>("Talos.Native", 1, 0, "CodeHighlighter");
    qmlRegisterType<talos::IconItem>("Talos.Native", 1, 0, "VectorPath");
    qmlRegisterType<talos::FlowChartItem>("Talos.Native", 1, 0, "FlowChart");
    qmlRegisterType<talos::FilePicker>("Talos.Native", 1, 0, "FilePicker");
    qmlRegisterUncreatableType<talos::Session>("Talos.Native", 1, 0, "Session",
                                               u"Session is provided by App"_s);

    // Before anything is loaded: nothing the window shows may fetch from
    // the network by being displayed.
    talos::NoNetworkFactory noNetwork;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&noNetwork);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &qapp,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Talos", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    app.attach(&engine, window);
    QDesktopServices::setUrlHandler(u"talon"_s, &app, "handleExternalUrl");

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QObject::connect(&instance, &QLocalServer::newConnection, &app, [&instance, &app] {
        QLocalSocket *socket = instance.nextPendingConnection();
        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QLocalSocket::readyRead, &app, [socket, &app] {
            app.handleExternalLink(QString::fromUtf8(socket->readLine()).trimmed());
        });
        if (socket->bytesAvailable() == 0 && socket->state() != QLocalSocket::ConnectedState)
            app.showWindow();
    });
#endif

    const bool startHidden = parser.isSet(hidden) && app.desktop()->trayAvailable()
        && app.store()->prefBool(u"closeToTray"_s);
    if (window && !startHidden)
        window->show();

    app.session()->start();
    if (!link.isEmpty())
        QTimer::singleShot(0, &app, [&app, link] { app.handleExternalLink(link); });
    if (parser.isSet(script)) {
        app.desktop()->setMuted(true);
        runScript(parser.value(script), &engine, &app);
    }

    const int rc = qapp.exec();
    QDesktopServices::unsetUrlHandler(u"talon"_s);
    return rc;
}
