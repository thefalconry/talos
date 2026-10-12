// SPDX-License-Identifier: GPL-2.0-only
#include "app/GlobalShortcut.h"

#include "core/AppLog.h"

#if defined(Q_OS_MACOS)
#  include <QGuiApplication>
#  include <Carbon/Carbon.h>
#endif
#if defined(TALOS_HAVE_DBUS)
#  include <QDBusArgument>
#  include <QDBusConnection>
#  include <QDBusConnectionInterface>
#  include <QDBusInterface>
#  include <QDBusMessage>
#  include <QDBusMetaType>
#  include <QDBusPendingCallWatcher>
#  include <QDBusPendingReply>
#endif

namespace talos {

using namespace Qt::StringLiterals;

#if defined(TALOS_HAVE_DBUS)

namespace {

const QString kPortal = u"org.freedesktop.portal.Desktop"_s;
const QString kPortalPath = u"/org/freedesktop/portal/desktop"_s;
const QString kShortcuts = u"org.freedesktop.portal.GlobalShortcuts"_s;
const QString kRequest = u"org.freedesktop.portal.Request"_s;
const QString kShortcutId = u"quick-ask"_s;

} // namespace

/// One entry of the portal's `a(sa{sv})` shortcut list.
struct PortalShortcut {
    QString id;
    QVariantMap properties;
};

QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s)
{
    arg.beginStructure();
    arg << s.id << s.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &s)
{
    arg.beginStructure();
    arg >> s.id >> s.properties;
    arg.endStructure();
    return arg;
}

} // namespace talos

Q_DECLARE_METATYPE(talos::PortalShortcut)

namespace talos {

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<QList<PortalShortcut>>();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()
        || !bus.interface()->isServiceRegistered(kPortal).value())
        return;
    // The portal answers for interfaces the desktop doesn't implement too,
    // so ask for the one property every implementation has.
    QDBusInterface properties(kPortal, kPortalPath, u"org.freedesktop.DBus.Properties"_s, bus);
    const QDBusMessage version = properties.call(u"Get"_s, kShortcuts, u"version"_s);
    m_available = version.type() == QDBusMessage::ReplyMessage;
}

QString GlobalShortcut::requestPath(const QString &token) const
{
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace(u'.', u'_');
    return u"%1/request/%2/%3"_s.arg(kPortalPath, sender, token);
}

void GlobalShortcut::bind()
{
    if (!m_available || m_state == "binding"_L1 || m_state == "bound"_L1)
        return;
    set(u"binding"_s);
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = u"talos_shortcut%1"_s.arg(++m_tokens);
    // Subscribed before the call: the answer may beat the call's return.
    bus.connect(kPortal, requestPath(token), kRequest, u"Response"_s, this,
                SLOT(onCreated(uint, QVariantMap)));
    QDBusMessage call = QDBusMessage::createMethodCall(kPortal, kPortalPath, kShortcuts,
                                                       u"CreateSession"_s);
    call.setArguments({QVariantMap{
        {u"handle_token"_s, token},
        {u"session_handle_token"_s, u"talos_shortcuts%1"_s.arg(m_tokens)},
    }});
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError())
            set(u"failed"_s, reply.error().message());
    });
}

void GlobalShortcut::onCreated(uint code, const QVariantMap &results)
{
    if (m_state != "binding"_L1 || !m_session.isEmpty())
        return;
    m_session = results.value(u"session_handle"_s).toString();
    if (code != 0 || m_session.isEmpty()) {
        m_session.clear();
        set(u"failed"_s, u"The desktop refused to set up a shortcut."_s);
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(kPortal, kPortalPath, kShortcuts, u"Activated"_s, this,
                SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    const QString token = u"talos_shortcut%1"_s.arg(++m_tokens);
    bus.connect(kPortal, requestPath(token), kRequest, u"Response"_s, this,
                SLOT(onBound(uint, QVariantMap)));
    const QList<PortalShortcut> wanted{{kShortcutId, {
        {u"description"_s, u"Ask Talon a quick question"_s},
        {u"preferred_trigger"_s, u"CTRL+ALT+space"_s},
    }}};
    QDBusMessage call = QDBusMessage::createMethodCall(kPortal, kPortalPath, kShortcuts,
                                                       u"BindShortcuts"_s);
    call.setArguments({QVariant::fromValue(QDBusObjectPath(m_session)),
                       QVariant::fromValue(wanted), QString(),
                       QVariantMap{{u"handle_token"_s, token}}});
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError())
            set(u"failed"_s, reply.error().message());
    });
}

void GlobalShortcut::onBound(uint code, const QVariantMap &results)
{
    if (m_state != "binding"_L1)
        return;
    if (code != 0) {
        release();
        set(u"failed"_s, code == 1 ? u"No shortcut was chosen."_s
                                   : u"The desktop couldn't bind the shortcut."_s);
        return;
    }
    m_trigger.clear();
    const QDBusArgument list = results.value(u"shortcuts"_s).value<QDBusArgument>();
    if (list.currentType() == QDBusArgument::ArrayType) {
        QList<PortalShortcut> bound;
        list >> bound;
        for (const PortalShortcut &s : bound) {
            if (s.id == kShortcutId)
                m_trigger = s.properties.value(u"trigger_description"_s).toString();
        }
    }
    AppLog::info("shortcut", u"quick-ask shortcut bound%1"_s
                     .arg(m_trigger.isEmpty() ? QString() : u" to "_s + m_trigger));
    set(u"bound"_s);
}

void GlobalShortcut::onActivated(const QDBusObjectPath &session, const QString &id, qulonglong,
                                 const QVariantMap &)
{
    if (session.path() == m_session && id == kShortcutId)
        emit activated();
}

void GlobalShortcut::release()
{
    if (!m_session.isEmpty()) {
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
            kPortal, m_session, u"org.freedesktop.portal.Session"_s, u"Close"_s));
        m_session.clear();
    }
    m_trigger.clear();
    set(u"off"_s);
}

#elif defined(Q_OS_MACOS)

// ── macOS: a hot key registered with the system ─────────────────────────────
//
// There is no portal to ask and no dialog to choose keys in: an app claims a
// combination, and the claim fails if another app already holds it. The
// keys are the ones the Linux side proposes, Control-Option-Space.

namespace {

EventHotKeyRef g_hotKey = nullptr;
EventHandlerRef g_handler = nullptr;

OSStatus onHotKey(EventHandlerCallRef, EventRef, void *self)
{
    emit static_cast<GlobalShortcut *>(self)->activated();
    return noErr;
}

} // namespace

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent)
{
    // Hot keys arrive through the app's own event loop; the background
    // service has none.
    m_available = qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr;
}

QString GlobalShortcut::requestPath(const QString &) const { return {}; }

void GlobalShortcut::bind()
{
    if (!m_available || m_state == "bound"_L1)
        return;
    if (!g_handler) {
        const EventTypeSpec pressed{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&onHotKey, 1, &pressed, this, &g_handler);
    }
    const EventHotKeyID id{FourCharCode('TALO'), 1};
    const OSStatus rc = RegisterEventHotKey(kVK_Space, controlKey | optionKey, id,
                                            GetApplicationEventTarget(), 0, &g_hotKey);
    if (rc != noErr) {
        g_hotKey = nullptr;
        set(u"failed"_s, rc == eventHotKeyExistsErr
                ? u"Another app already uses Control-Option-Space."_s
                : u"macOS refused the shortcut (error %1)."_s.arg(rc));
        return;
    }
    m_trigger = u"⌃⌥Space"_s;
    AppLog::info("shortcut", u"quick-ask shortcut bound to Control-Option-Space"_s);
    set(u"bound"_s);
}

void GlobalShortcut::release()
{
    if (g_hotKey) {
        UnregisterEventHotKey(g_hotKey);
        g_hotKey = nullptr;
    }
    m_trigger.clear();
    set(u"off"_s);
}

#else // no D-Bus

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {}
QString GlobalShortcut::requestPath(const QString &) const { return {}; }
void GlobalShortcut::bind() {}
void GlobalShortcut::release() {}

#endif

void GlobalShortcut::set(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit changed();
}

} // namespace talos
