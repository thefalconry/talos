// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/ScreenControl.h"

#include "core/AppLog.h"
#include "core/Store.h"

#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImageWriter>
#include <QJsonArray>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QtMath>

#if defined(TALOS_HAVE_DBUS)
#  include <QDBusArgument>
#  include <QDBusConnection>
#  include <QDBusInterface>
#  include <QDBusMessage>
#  include <QDBusObjectPath>
#  include <QDBusPendingCallWatcher>
#  include <QDBusPendingReply>
#  include <QDBusReply>
#endif

namespace talos {

using namespace Qt::StringLiterals;

namespace {

constexpr int kMaxImageBytes = 300 * 1024;
constexpr int kMinImageBytes = 32 * 1024;
constexpr int kFitQualityFloor = 35; // below this JPEG text turns to mush; shrink instead
constexpr int kFitLastQuality = 20;
constexpr int kFitMinEdge = 320;

// evdev button codes and X11 keysyms: what the portal's Notify* calls take.
constexpr int kButtonLeft = 0x110;
constexpr int kButtonRight = 0x111;
constexpr int kButtonMiddle = 0x112;
constexpr int kKeyReturn = 0xff0d;
constexpr int kKeyTab = 0xff09;

const QHash<QString, int> &namedKeys()
{
    static const QHash<QString, int> keys{
        {u"escape"_s, 0xff1b}, {u"esc"_s, 0xff1b}, {u"return"_s, kKeyReturn},
        {u"enter"_s, kKeyReturn}, {u"tab"_s, kKeyTab}, {u"backspace"_s, 0xff08},
        {u"delete"_s, 0xffff}, {u"del"_s, 0xffff}, {u"insert"_s, 0xff63},
        {u"space"_s, 0x20}, {u"up"_s, 0xff52}, {u"down"_s, 0xff54}, {u"left"_s, 0xff51},
        {u"right"_s, 0xff53}, {u"home"_s, 0xff50}, {u"end"_s, 0xff57},
        {u"pageup"_s, 0xff55}, {u"pagedown"_s, 0xff56}, {u"capslock"_s, 0xffe5},
        {u"print"_s, 0xff61}, {u"menu"_s, 0xff67}, {u"plus"_s, 0x2b}, {u"minus"_s, 0x2d},
    };
    return keys;
}

const QHash<QString, int> &modifierKeys()
{
    // The names talon-node accepts; "cmd" is the Super key off a Mac.
    static const QHash<QString, int> keys{
        {u"shift"_s, 0xffe1}, {u"ctrl"_s, 0xffe3}, {u"control"_s, 0xffe3},
        {u"alt"_s, 0xffe9}, {u"opt"_s, 0xffe9}, {u"option"_s, 0xffe9},
        {u"cmd"_s, 0xffeb}, {u"command"_s, 0xffeb}, {u"meta"_s, 0xffeb},
        {u"super"_s, 0xffeb}, {u"win"_s, 0xffeb},
    };
    return keys;
}

/// The keysym that types @p c: Latin-1 maps to itself, the rest to the
/// Unicode keysym range.
int keysymForChar(char32_t c)
{
    if (c == U'\n')
        return kKeyReturn;
    if (c == U'\t')
        return kKeyTab;
    if ((c >= 0x20 && c <= 0x7e) || (c >= 0xa0 && c <= 0xff))
        return int(c);
    return int(0x01000000 | c);
}

} // namespace

// ── Pure parts ──────────────────────────────────────────────────────────────

QSize ScreenControl::spaceFor(const QSize &screen, int maxEdge, double *factor)
{
    const int edge = qBound(320, maxEdge > 0 ? maxEdge : 1280, 4096);
    const int longest = qMax(screen.width(), screen.height());
    const double f = longest > edge ? double(longest) / edge : 1.0;
    if (factor)
        *factor = f;
    return {qRound(screen.width() / f), qRound(screen.height() / f)};
}

int ScreenControl::keysymFor(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (const int sym = namedKeys().value(key))
        return sym;
    if (key.size() >= 2 && key.at(0) == u'f') {
        bool ok = false;
        const int n = key.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24)
            return 0xffbe + n - 1;
    }
    const auto chars = name.trimmed().toUcs4();
    return chars.size() == 1 ? keysymForChar(chars.first()) : 0;
}

bool ScreenControl::parseCombo(const QString &combo, QList<int> *modifiers, int *key,
                               QString *error)
{
    const QString text = combo.trimmed();
    if (text.isEmpty()) {
        *error = u"key needs keys, e.g. \"ctrl+s\""_s;
        return false;
    }
    // "+" alone, or a combo ending in "++", names the plus key itself.
    QStringList parts;
    if (text == "+"_L1) {
        parts = {u"+"_s};
    } else if (text.endsWith("++"_L1)) {
        parts = text.chopped(2).split(u'+');
        parts << u"+"_s;
    } else {
        parts = text.split(u'+');
    }
    for (int i = 0; i < parts.size() - 1; ++i) {
        const int sym = modifierKeys().value(parts.at(i).trimmed().toLower());
        if (!sym) {
            *error = u"unknown modifier \"%1\""_s.arg(parts.at(i));
            return false;
        }
        modifiers->append(sym);
    }
    const QString last = parts.last().trimmed();
    // A lone modifier is a valid key press too.
    *key = modifierKeys().value(last.toLower());
    if (!*key)
        *key = keysymFor(last);
    if (!*key) {
        *error = u"unknown key \"%1\""_s.arg(last);
        return false;
    }
    return true;
}

int ScreenControl::macKeyCode(int keysym)
{
    // Carbon's kVK_* codes: positions on the keyboard, not characters.
    static const QHash<int, int> codes{
        {0xff1b, 53}, {kKeyReturn, 36}, {kKeyTab, 48}, {0xff08, 51}, {0xffff, 117},
        {0xff63, 114}, {0x20, 49}, {0xff52, 126}, {0xff54, 125}, {0xff51, 123},
        {0xff53, 124}, {0xff50, 115}, {0xff57, 119}, {0xff55, 116}, {0xff56, 121},
        {0xffe5, 57}, {0xff61, 105}, {0xff67, 110},
        {0xffe1, 56}, {0xffe3, 59}, {0xffe9, 58}, {0xffeb, 55},
    };
    static const int function[] = {122, 120, 99, 118, 96, 97, 98, 100, 101, 109,
                                   103, 111, 105, 107, 113, 106, 64, 79, 80, 90};
    if (keysym >= 0xffbe && keysym < 0xffbe + int(std::size(function)))
        return function[keysym - 0xffbe];
    return codes.value(keysym, -1);
}

QByteArray ScreenControl::fitJpeg(const QImage &image, int quality, int maxBytes, QSize *size,
                                  int *usedQuality, QString *error)
{
    quality = qBound(kFitLastQuality, quality, 90);
    const int longest = qMax(image.width(), image.height());
    const double minScale = longest > kFitMinEdge ? double(kFitMinEdge) / longest : 1.0;
    double scale = 1.0;
    for (int step = 0; step < 16; ++step) {
        const QSize target(qMax(1, qRound(image.width() * scale)),
                           qMax(1, qRound(image.height() * scale)));
        const QImage scaled = target == image.size()
            ? image : image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QImageWriter writer(&buffer, "jpeg");
        writer.setQuality(quality);
        if (!writer.write(scaled)) {
            *error = u"could not encode the screenshot: %1"_s.arg(writer.errorString());
            return {};
        }
        if (bytes.size() <= maxBytes) {
            *size = target;
            *usedQuality = quality;
            return bytes;
        }
        if (quality > kFitQualityFloor) {
            quality = qMax(kFitQualityFloor, quality - 20);
        } else if (scale > minScale) {
            const double ratio = qSqrt(double(maxBytes) / bytes.size()) * 0.9;
            scale = qMax(minScale, scale * qBound(0.4, ratio, 0.9));
        } else if (quality > kFitLastQuality) {
            quality = kFitLastQuality;
        } else {
            *error = u"the screen does not fit in %1 bytes even at %2x%3"_s
                         .arg(maxBytes).arg(target.width()).arg(target.height());
            return {};
        }
    }
    *error = u"gave up fitting the screenshot into %1 bytes"_s.arg(maxBytes);
    return {};
}

ScreenControl::ScreenControl(Store *store, QObject *parent) : QObject(parent), m_store(store) {}

QJsonObject ScreenControl::baseReply(const QSize &space) const
{
    double f = 1;
    spaceFor(m_streamSize, qMax(space.width(), space.height()), &f);
    // Wayland does not tell a client where the pointer is. What is reported
    // is where this session last put it.
    const bool known = m_cursor.x() >= 0;
    return {
        {"space"_L1, QJsonArray{space.width(), space.height()}},
        {"cursor"_L1, QJsonArray{known ? qRound(m_cursor.x() / f) : 0,
                                 known ? qRound(m_cursor.y() / f) : 0}},
        {"cursorKnown"_L1, known},
        {"trusted"_L1, m_ready},
    };
}

void ScreenControl::runSteps(QList<Step> steps, std::function<void()> then)
{
    auto queue = std::make_shared<QList<Step>>(std::move(steps));
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, queue, next, then] {
        if (queue->isEmpty()) {
            then();
            return;
        }
        const Step step = queue->takeFirst();
        step.run();
        QTimer::singleShot(qMax(1, step.thenWaitMs), this, [next] { (*next)(); });
    };
    (*next)();
}

void ScreenControl::handle(const QJsonObject &params, DeviceExec::Done done)
{
    static const QStringList actions{u"screenshot"_s, u"snapshot"_s, u"click"_s, u"move"_s,
                                     u"drag"_s, u"scroll"_s, u"type"_s, u"key"_s};
    const QString action = params["action"_L1].toString();
    if (!actions.contains(action)) {
        done(CommandOutcome::fail(u"computer: unknown action \"%1\" (one of: %2)."_s
                                      .arg(action, actions.join(u", "_s))));
        return;
    }
    if (action == "snapshot"_L1 && !canSnapshot()) {
        done(CommandOutcome::fail(u"computer snapshot: reading the accessibility tree is not "
                                  "available on this device. Use screenshot instead."_s));
        return;
    }
    if (!available()) {
        done(CommandOutcome::fail(u"computer: this desktop offers no way to control it."_s));
        return;
    }
    // One action at a time: two interleaved drags would be nonsense.
    if (m_busy) {
        done(CommandOutcome::fail(u"computer: another action is still running."_s));
        return;
    }
    m_busy = true;
    m_action = action;
    const DeviceExec::Done finish = [this, done](const CommandOutcome &o) {
        m_busy = false;
        done(o);
    };
    ensureSession([this, params, action, finish](const QString &error) {
        if (!error.isEmpty()) {
            finish(CommandOutcome::fail(u"computer %1: %2"_s.arg(action, error)));
            return;
        }
        perform(params, finish);
    });
}

void ScreenControl::perform(const QJsonObject &params, DeviceExec::Done done)
{
    const QString action = params["action"_L1].toString();
    if (action == "screenshot"_L1) {
        screenshot(params, done);
        return;
    }
    if (action == "snapshot"_L1) {
        snapshot(params, done);
        return;
    }
    double f = 1;
    const QSize space = spaceFor(m_streamSize, params["maxEdge"_L1].toInt(1280), &f);
    const auto fail = [&](const QString &why) {
        done(CommandOutcome::fail(u"computer %1: %2"_s.arg(action, why)));
    };
    // A point in the space, as stream coordinates.
    const auto point = [&](QLatin1StringView kx, QLatin1StringView ky, QPointF *out) {
        if (!params[kx].isDouble() || !params[ky].isDouble()) {
            fail(u"needs %1 and %2"_s.arg(kx, ky));
            return false;
        }
        const double x = params[kx].toDouble(), y = params[ky].toDouble();
        if (x < 0 || y < 0 || x > space.width() || y > space.height()) {
            fail(u"(%1,%2) is outside the %3x%4 space"_s.arg(x).arg(y)
                     .arg(space.width()).arg(space.height()));
            return false;
        }
        *out = QPointF(qMin(x * f, m_streamSize.width() - 1.0),
                       qMin(y * f, m_streamSize.height() - 1.0));
        return true;
    };
    const QString session = m_session;
    const auto moveTo = [this](const QPointF &p) {
        return Step{[this, p] {
            notify(u"NotifyPointerMotionAbsolute"_s,
                   {QVariantMap(), QVariant::fromValue<uint>(m_stream), p.x(), p.y()});
            m_cursor = p;
        }, 40};
    };
    const auto button = [this](int code, bool down, int wait) {
        return Step{[this, code, down] {
            notify(u"NotifyPointerButton"_s,
                   {QVariantMap(), code, QVariant::fromValue<uint>(down ? 1 : 0)});
        }, wait};
    };
    const auto key = [this](int sym, bool down, int wait) {
        return Step{[this, sym, down] {
            notify(u"NotifyKeyboardKeysym"_s,
                   {QVariantMap(), sym, QVariant::fromValue<uint>(down ? 1 : 0)});
        }, wait};
    };

    QList<Step> steps;
    QJsonObject extra;
    // Set by a click that is to be checked: what it hit, read once the
    // interface has had a moment to react.
    std::function<void(QJsonObject *)> verify;
    int settleMs = 0;
    if (action == "move"_L1) {
        QPointF p;
        if (!point("x"_L1, "y"_L1, &p))
            return;
        steps << moveTo(p);
    } else if (action == "click"_L1) {
        QPointF p;
        if (!point("x"_L1, "y"_L1, &p))
            return;
        const QString name = params["button"_L1].toString(u"left"_s).toLower();
        const int code = name == "left"_L1 ? kButtonLeft : name == "right"_L1 ? kButtonRight
                       : name == "middle"_L1 ? kButtonMiddle : 0;
        if (!code)
            return fail(u"unknown button \"%1\""_s.arg(name));
        QList<int> mods;
        for (const auto &m : params["modifiers"_L1].toArray()) {
            const int sym = modifierKeys().value(m.toString().toLower());
            if (!sym)
                return fail(u"unknown modifier \"%1\""_s.arg(m.toString()));
            mods << sym;
        }
        const int count = qBound(1, params["count"_L1].toInt(1), 3);
        if (params["verify"_L1].toBool(true)) {
            verify = clickProbe(p, f);
            settleMs = qBound(0, params["settleMs"_L1].toInt(400), 3000);
        }
        steps << moveTo(p);
        for (const int sym : mods)
            steps << key(sym, true, 10);
        for (int i = 0; i < count; ++i)
            steps << button(code, true, 20) << button(code, false, 60);
        for (auto it = mods.rbegin(); it != mods.rend(); ++it)
            steps << key(*it, false, 10);
    } else if (action == "drag"_L1) {
        QPointF from, to;
        if (!point("x"_L1, "y"_L1, &from) || !point("toX"_L1, "toY"_L1, &to))
            return;
        steps << moveTo(from) << button(kButtonLeft, true, 80);
        constexpr int kHops = 12;
        for (int i = 1; i <= kHops; ++i) {
            Step hop = moveTo(from + (to - from) * i / kHops);
            hop.thenWaitMs = 15;
            steps << hop;
        }
        steps << Step{[] {}, 50} << button(kButtonLeft, false, 30);
    } else if (action == "scroll"_L1) {
        const int dy = qBound(-50, params["dy"_L1].toInt(), 50);
        const int dx = qBound(-50, params["dx"_L1].toInt(), 50);
        if (!dy && !dx)
            return fail(u"needs a non-zero dy or dx"_s);
        if (params.contains("x"_L1) || params.contains("y"_L1)) {
            QPointF p;
            if (!point("x"_L1, "y"_L1, &p))
                return;
            steps << moveTo(p);
        }
        // Positive dy moves down the page, as on the node.
        if (dy) {
            steps << Step{[this, dy] {
                notify(u"NotifyPointerAxisDiscrete"_s,
                       {QVariantMap(), QVariant::fromValue<uint>(0), dy});
            }, 50};
        }
        if (dx) {
            steps << Step{[this, dx] {
                notify(u"NotifyPointerAxisDiscrete"_s,
                       {QVariantMap(), QVariant::fromValue<uint>(1), dx});
            }, 50};
        }
    } else if (action == "type"_L1) {
        QString text = params["text"_L1].toString();
        if (text.isEmpty())
            return fail(u"needs text"_s);
        if (text.size() > 4000)
            return fail(u"is limited to 4000 characters"_s);
        text.replace("\r\n"_L1, "\n"_L1).replace(u'\r', u'\n');
        for (const char32_t c : text.toUcs4()) {
            const int sym = keysymForChar(c);
            steps << key(sym, true, 4) << key(sym, false, 8);
        }
        extra["typed"_L1] = text.size();
    } else if (action == "key"_L1) {
        QList<int> mods;
        int sym = 0;
        QString why;
        if (!parseCombo(params["keys"_L1].toString(), &mods, &sym, &why))
            return fail(why);
        for (const int m : mods)
            steps << key(m, true, 10);
        steps << key(sym, true, 20) << key(sym, false, 20);
        for (auto it = mods.rbegin(); it != mods.rend(); ++it)
            steps << key(*it, false, 10);
    }

    runSteps(steps, [this, done, space, extra, session, verify, settleMs] {
        if (session != m_session || !m_ready) {
            done(CommandOutcome::fail(u"computer: the session was ended on the device."_s));
            return;
        }
        const auto answer = [this, done, space, extra, verify] {
            QJsonObject data = baseReply(space);
            for (auto it = extra.begin(); it != extra.end(); ++it)
                data[it.key()] = it.value();
            if (verify)
                verify(&data);
            done(CommandOutcome::done(data));
        };
        if (verify)
            QTimer::singleShot(settleMs, this, answer);
        else
            answer();
    });
}

void ScreenControl::replyWithShot(QImage shot, const QJsonObject &params, DeviceExec::Done done)
{
    double f = 1;
    const QSize space = spaceFor(m_streamSize, params["maxEdge"_L1].toInt(1280), &f);
    shot = shot.scaled(space, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
               .convertToFormat(QImage::Format_RGB888);

    const int maxBytes = qBound(kMinImageBytes, params["maxBytes"_L1].toInt(kMaxImageBytes),
                                kMaxImageBytes);
    QSize size;
    int quality = 0;
    QString error;
    const QByteArray jpeg =
        fitJpeg(shot, params["quality"_L1].toInt(70), maxBytes, &size, &quality, &error);
    if (jpeg.isEmpty()) {
        done(CommandOutcome::fail(u"computer screenshot: %1"_s.arg(error)));
        return;
    }
    QJsonObject data = baseReply(space);
    data["base64"_L1] = QString::fromLatin1(jpeg.toBase64());
    data["mimeType"_L1] = u"image/jpeg"_s;
    data["width"_L1] = size.width();
    data["height"_L1] = size.height();
    // Multiply a position in the picture by this to get the space.
    data["scale"_L1] = qRound(1000.0 * space.width() / size.width()) / 1000.0;
    data["quality"_L1] = quality;
    data["bytes"_L1] = jpeg.size();
    done(CommandOutcome::done(data));
}

void ScreenControl::onSessionClosed()
{
    if (!m_ready && m_session.isEmpty())
        return;
    AppLog::info("screen", u"the desktop ended the screen-control session"_s);
    m_ready = false;
    m_session.clear();
    emit changed();
}

#if defined(TALOS_HAVE_DBUS)

namespace {

const QString kPortal = u"org.freedesktop.portal.Desktop"_s;
const QString kPortalPath = u"/org/freedesktop/portal/desktop"_s;
const QString kRemote = u"org.freedesktop.portal.RemoteDesktop"_s;

/// One portal request: the method call plus the `Response` signal that
/// carries its real answer, which may be a dialog's worth of time away.
class PortalCall : public QObject
{
    Q_OBJECT
public:
    using Done = std::function<void(uint code, const QVariantMap &results)>;

    PortalCall(const QString &iface, const QString &method, QVariantList args,
               QVariantMap options, int token, Done done, QObject *parent)
        : QObject(parent), m_done(std::move(done))
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        const QString handle = u"talos%1"_s.arg(token);
        QString sender = bus.baseService().mid(1);
        sender.replace(u'.', u'_');
        // Subscribe before calling: the answer can beat the call's return.
        m_path = u"%1/request/%2/%3"_s.arg(kPortalPath, sender, handle);
        bus.connect(kPortal, m_path, u"org.freedesktop.portal.Request"_s, u"Response"_s, this,
                    SLOT(response(uint, QVariantMap)));
        options[u"handle_token"_s] = handle;
        args << options;
        QDBusMessage call = QDBusMessage::createMethodCall(kPortal, kPortalPath, iface, method);
        call.setArguments(args);
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher *w) {
            const QDBusPendingReply<QDBusObjectPath> reply = *w;
            w->deleteLater();
            if (reply.isError())
                finish(2, {{u"error"_s, reply.error().message()}});
        });
        // A consent dialog can sit there for a while, but not forever.
        QTimer::singleShot(180'000, this, [this] {
            finish(2, {{u"error"_s, u"nobody answered the desktop's permission prompt"_s}});
        });
    }

public slots:
    void response(uint code, const QVariantMap &results) { finish(code, results); }

private:
    void finish(uint code, const QVariantMap &results)
    {
        if (!m_done)
            return;
        const Done done = std::move(m_done);
        m_done = nullptr;
        QDBusConnection::sessionBus().disconnect(
            kPortal, m_path, u"org.freedesktop.portal.Request"_s, u"Response"_s, this,
            SLOT(response(uint, QVariantMap)));
        deleteLater();
        done(code, results);
    }

    Done m_done;
    QString m_path;
};

QString portalError(uint code, const QVariantMap &results)
{
    if (code == 1)
        return u"the request was declined on the device"_s;
    const QString detail = results.value(u"error"_s).toString();
    return detail.isEmpty() ? u"the desktop portal refused the request"_s : detail;
}

} // namespace

ScreenControl::~ScreenControl()
{
    if (!m_session.isEmpty()) {
        QDBusConnection::sessionBus().call(
            QDBusMessage::createMethodCall(kPortal, m_session,
                                           u"org.freedesktop.portal.Session"_s, u"Close"_s),
            QDBus::NoBlock);
    }
}

bool ScreenControl::available() const
{
    static int cached = -1;
    if (cached < 0) {
        // Needs the app's own screens; the background service has none.
        if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            cached = 0;
        } else {
            QDBusInterface props(kPortal, kPortalPath, u"org.freedesktop.DBus.Properties"_s,
                                 QDBusConnection::sessionBus());
            const QDBusReply<QVariant> version =
                props.call(u"Get"_s, kRemote, u"AvailableDeviceTypes"_s);
            // Keyboard (1) and pointer (2) are both needed.
            cached = version.isValid() && (version.value().toUInt() & 3) == 3 ? 1 : 0;
        }
    }
    return cached == 1;
}

void ScreenControl::notify(const QString &method, const QVariantList &args)
{
    if (m_session.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(kPortal, kPortalPath, kRemote, method);
    QVariantList full{QVariant::fromValue(QDBusObjectPath(m_session))};
    full += args;
    call.setArguments(full);
    QDBusConnection::sessionBus().call(call, QDBus::NoBlock);
}

void ScreenControl::ensureSession(std::function<void(const QString &)> then)
{
    if (m_ready) {
        then({});
        return;
    }
    m_waiters << std::move(then);
    if (m_starting)
        return;
    m_starting = true;
    const auto settle = [this](const QString &error) {
        m_starting = false;
        m_ready = error.isEmpty();
        if (!m_ready)
            m_session.clear();
        const auto waiters = std::move(m_waiters);
        m_waiters.clear();
        emit changed();
        for (const auto &w : waiters)
            w(error);
    };

    const QString sessionToken = u"talos_session%1"_s.arg(++m_tokens);
    new PortalCall(kRemote, u"CreateSession"_s, {},
                   {{u"session_handle_token"_s, sessionToken}}, ++m_tokens,
                   [this, settle](uint code, const QVariantMap &created) {
        if (code != 0)
            return settle(portalError(code, created));
        m_session = created.value(u"session_handle"_s).toString();
        if (m_session.isEmpty())
            return settle(u"the desktop portal gave no session"_s);
        QDBusConnection::sessionBus().connect(kPortal, m_session,
                                              u"org.freedesktop.portal.Session"_s, u"Closed"_s,
                                              this, SLOT(onSessionClosed()));
        const QDBusObjectPath session(m_session);

        // Keyboard and pointer, remembered until revoked so the consent
        // dialog is a one-time thing.
        QVariantMap devices{
            {u"types"_s, QVariant::fromValue<uint>(3)},
            {u"persist_mode"_s, QVariant::fromValue<uint>(2)},
        };
        const QString restore = m_store->prefString(u"screenRestoreToken"_s);
        if (!restore.isEmpty())
            devices[u"restore_token"_s] = restore;
        new PortalCall(kRemote, u"SelectDevices"_s, {QVariant::fromValue(session)}, devices,
                       ++m_tokens, [this, settle, session](uint code, const QVariantMap &r) {
            if (code != 0)
                return settle(portalError(code, r));
            // One monitor: absolute pointer positions are relative to it.
            new PortalCall(u"org.freedesktop.portal.ScreenCast"_s, u"SelectSources"_s,
                           {QVariant::fromValue(session)},
                           {{u"types"_s, QVariant::fromValue<uint>(1)}, {u"multiple"_s, false}},
                           ++m_tokens, [this, settle, session](uint code, const QVariantMap &r) {
                if (code != 0)
                    return settle(portalError(code, r));
                new PortalCall(kRemote, u"Start"_s, {QVariant::fromValue(session), QString()}, {},
                               ++m_tokens, [this, settle](uint code, const QVariantMap &started) {
                    if (code != 0)
                        return settle(portalError(code, started));
                    m_stream = 0;
                    m_streamSize = {};
                    m_streamPos = {};
                    // streams: a(ua{sv}), each with a size and a position.
                    const QDBusArgument streams =
                        started.value(u"streams"_s).value<QDBusArgument>();
                    if (streams.currentType() == QDBusArgument::ArrayType) {
                        streams.beginArray();
                        if (!streams.atEnd()) {
                            QVariantMap props;
                            streams.beginStructure();
                            streams >> m_stream >> props;
                            streams.endStructure();
                            const auto pair = [&props](const QString &key, int *a, int *b) {
                                const QDBusArgument v = props.value(key).value<QDBusArgument>();
                                if (v.currentType() != QDBusArgument::StructureType)
                                    return;
                                v.beginStructure();
                                v >> *a >> *b;
                                v.endStructure();
                            };
                            int w = 0, h = 0, x = 0, y = 0;
                            pair(u"size"_s, &w, &h);
                            pair(u"position"_s, &x, &y);
                            m_streamSize = QSize(w, h);
                            m_streamPos = QPoint(x, y);
                        }
                        streams.endArray();
                    }
                    if (!m_stream || m_streamSize.isEmpty())
                        return settle(u"the desktop shared no screen"_s);
                    const QString token = started.value(u"restore_token"_s).toString();
                    if (!token.isEmpty())
                        m_store->setPref(u"screenRestoreToken"_s, token);
                    m_cursor = QPointF(-1, -1);
                    AppLog::info("screen", u"screen control granted (%1x%2)"_s
                                               .arg(m_streamSize.width())
                                               .arg(m_streamSize.height()));
                    settle({});
                }, this);
            }, this);
        }, this);
    }, this);
}

void ScreenControl::screenshot(const QJsonObject &params, DeviceExec::Done done)
{
    new PortalCall(u"org.freedesktop.portal.Screenshot"_s, u"Screenshot"_s, {QString()},
                   {{u"interactive"_s, false}}, ++m_tokens,
                   [this, params, done](uint code, const QVariantMap &r) {
        if (code != 0) {
            done(CommandOutcome::fail(u"computer screenshot: %1"_s.arg(portalError(code, r))));
            return;
        }
        const QString file = QUrl(r.value(u"uri"_s).toString()).toLocalFile();
        QImage shot(file);
        // The portal wrote it to the user's pictures; it was only ever
        // meant for this reply.
        QFile::remove(file);
        if (shot.isNull()) {
            done(CommandOutcome::fail(u"computer screenshot: the desktop returned no image."_s));
            return;
        }
        // The picture covers every monitor; the pointer works on one. Cut
        // out the one that was shared.
        QRect all;
        for (const QScreen *screen : QGuiApplication::screens())
            all |= screen->geometry();
        if (!all.isEmpty() && all.size() != m_streamSize) {
            const double kx = double(shot.width()) / all.width();
            const double ky = double(shot.height()) / all.height();
            const QRect cut(qRound((m_streamPos.x() - all.x()) * kx),
                            qRound((m_streamPos.y() - all.y()) * ky),
                            qRound(m_streamSize.width() * kx),
                            qRound(m_streamSize.height() * ky));
            if (shot.rect().contains(cut))
                shot = shot.copy(cut);
        }
        replyWithShot(shot, params, done);
    }, this);
}

bool ScreenControl::canSnapshot() { return false; }
void ScreenControl::snapshot(const QJsonObject &, DeviceExec::Done) {}
std::function<void(QJsonObject *)> ScreenControl::clickProbe(const QPointF &, double)
{
    return {};
}

void ScreenControl::revoke()
{
    m_store->setPref(u"screenRestoreToken"_s, QString());
    if (!m_session.isEmpty()) {
        QDBusConnection::sessionBus().call(
            QDBusMessage::createMethodCall(kPortal, m_session,
                                           u"org.freedesktop.portal.Session"_s, u"Close"_s),
            QDBus::NoBlock);
    }
    m_session.clear();
    m_ready = false;
    emit changed();
}

#elif !defined(Q_OS_MACOS) // no D-Bus: no portal. The Mac's is ScreenControlMac.mm.

ScreenControl::~ScreenControl() = default;
bool ScreenControl::available() const { return false; }
void ScreenControl::notify(const QString &, const QVariantList &) {}
void ScreenControl::ensureSession(std::function<void(const QString &)> then)
{
    then(u"this platform has no screen-control backend yet"_s);
}
void ScreenControl::screenshot(const QJsonObject &, DeviceExec::Done done)
{
    done(CommandOutcome::fail(u"computer screenshot: not available on this platform."_s));
}
void ScreenControl::revoke() {}
bool ScreenControl::canSnapshot() { return false; }
void ScreenControl::snapshot(const QJsonObject &, DeviceExec::Done) {}
std::function<void(QJsonObject *)> ScreenControl::clickProbe(const QPointF &, double)
{
    return {};
}

#endif

} // namespace talos

#if defined(TALOS_HAVE_DBUS)
#  include "ScreenControl.moc"
#endif
