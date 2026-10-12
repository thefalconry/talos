// SPDX-License-Identifier: GPL-2.0-only
//
// ScreenControl on macOS. The shared half (ScreenControl.cpp) plans an
// action as the portal calls a Linux desktop takes; here those calls become
// CoreGraphics events, and the accessibility tree is read with the
// AXUIElement API. What a snapshot reports and how a click is checked
// follow talon-node's macOS driver, so the daemon sees one contract.
#include "mesh/ScreenControl.h"

#include "core/AppLog.h"
#include "core/Store.h"

#include <QDesktopServices>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>
#include <QtMath>

#include <optional>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

// ── Core Foundation ownership ───────────────────────────────────────────────

/// Owns one reference to a Core Foundation object.
template <typename T>
class Cf
{
public:
    Cf() = default;
    /// Takes over a reference the caller owns (a Create or Copy result).
    explicit Cf(T ref) : m_ref(ref) {}
    Cf(const Cf &other) : m_ref(other.m_ref)
    {
        if (m_ref)
            CFRetain(m_ref);
    }
    Cf(Cf &&other) noexcept : m_ref(other.m_ref) { other.m_ref = nullptr; }
    Cf &operator=(Cf other) noexcept
    {
        std::swap(m_ref, other.m_ref);
        return *this;
    }
    ~Cf()
    {
        if (m_ref)
            CFRelease(m_ref);
    }
    static Cf retained(T ref)
    {
        if (ref)
            CFRetain(ref);
        return Cf(ref);
    }
    T get() const { return m_ref; }
    explicit operator bool() const { return m_ref != nullptr; }

private:
    T m_ref = nullptr;
};

using Element = Cf<AXUIElementRef>;

// ── Pointer and keyboard ────────────────────────────────────────────────────

constexpr CGEventFlags kModifierMask = kCGEventFlagMaskShift | kCGEventFlagMaskControl
    | kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand;

struct KeyStroke {
    CGKeyCode code = 0;
    bool shift = false;
};

/// What an action has pressed so far. One action runs at a time, and each
/// starts from nothing held.
struct Input {
    CGEventFlags flags = 0;
    int held = -1;       ///< the mouse button that is down, for drags
    int lastButton = -1;
    int clicks = 0;      ///< presses in a row at one spot: 2 is a double click
    CGPoint at{};
    CGPoint lastDown{};
    QHash<char32_t, KeyStroke> layout;
};
Input g_input;

CGPoint pointerNow()
{
    const Cf<CGEventRef> probe(CGEventCreate(nullptr));
    return probe ? CGEventGetLocation(probe.get()) : CGPoint{};
}

void post(CGEventRef event)
{
    if (!event)
        return;
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

/// Which key types each character on the keyboard layout in use, so that
/// "cmd+z" presses the key labelled Z whatever the layout is.
QHash<char32_t, KeyStroke> readLayout()
{
    QHash<char32_t, KeyStroke> keys;
    const Cf<TISInputSourceRef> source(TISCopyCurrentKeyboardLayoutInputSource());
    if (!source)
        return keys;
    const auto data = static_cast<CFDataRef>(
        TISGetInputSourceProperty(source.get(), kTISPropertyUnicodeKeyLayoutData));
    if (!data)
        return keys;
    const auto *layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
    // Unshifted first, so a character both ways is typed the plain way.
    for (const bool shift : {false, true}) {
        // Below the keypad and the function keys, which repeat characters
        // the main block already has.
        for (UInt16 code = 0; code < 64; ++code) {
            UInt32 dead = 0;
            UniChar chars[4] = {};
            UniCharCount count = 0;
            const OSStatus rc = UCKeyTranslate(layout, code, kUCKeyActionDown,
                                               shift ? (shiftKey >> 8) & 0xff : 0, LMGetKbdType(),
                                               kUCKeyTranslateNoDeadKeysMask, &dead, 4, &count,
                                               chars);
            if (rc != noErr || count != 1 || chars[0] <= 0x20 || chars[0] == 0x7f)
                continue;
            if (!keys.contains(chars[0]))
                keys.insert(chars[0], {CGKeyCode(code), shift});
        }
    }
    return keys;
}

void keyEvent(int keysym, bool down)
{
    static const struct {
        int sym;
        CGEventFlags flag;
    } modifiers[] = {
        {0xffe1, kCGEventFlagMaskShift},
        {0xffe3, kCGEventFlagMaskControl},
        {0xffe9, kCGEventFlagMaskAlternate},
        {0xffeb, kCGEventFlagMaskCommand},
    };
    for (const auto &m : modifiers) {
        if (m.sym != keysym)
            continue;
        if (down)
            g_input.flags |= m.flag;
        else
            g_input.flags &= ~m.flag;
        break;
    }
    const auto send = [down](CGKeyCode code, CGEventFlags flags, const QString &text = {}) {
        CGEventRef event = CGEventCreateKeyboardEvent(nullptr, code, down);
        if (!event)
            return;
        if (!text.isEmpty()) {
            CGEventKeyboardSetUnicodeString(event, UniCharCount(text.size()),
                                            reinterpret_cast<const UniChar *>(text.utf16()));
        }
        // Keep what the system put there itself (arrows are "function"
        // keys), replace only the modifiers.
        CGEventSetFlags(event, (CGEventGetFlags(event) & ~kModifierMask) | flags);
        post(event);
    };
    const int named = ScreenControl::macKeyCode(keysym);
    if (named >= 0) {
        send(CGKeyCode(named), g_input.flags);
        return;
    }
    const char32_t c = keysym >= 0x01000000 ? char32_t(keysym & 0x00ffffff) : char32_t(keysym);
    // With a modifier held this is a shortcut: it is the key that counts,
    // so press the one that carries the character.
    if (g_input.flags) {
        const auto key = g_input.layout.constFind(QChar::toLower(c));
        if (key != g_input.layout.constEnd()) {
            send(key->code, g_input.flags | (key->shift ? kCGEventFlagMaskShift : 0));
            return;
        }
    }
    // Plain text is typed as the character itself, whatever the layout.
    send(0, g_input.flags, QString::fromUcs4(&c, 1));
}

void mouseMove(CGPoint to)
{
    CGEventType type = kCGEventMouseMoved;
    CGMouseButton button = kCGMouseButtonLeft;
    if (g_input.held == kCGMouseButtonLeft) {
        type = kCGEventLeftMouseDragged;
    } else if (g_input.held == kCGMouseButtonRight) {
        type = kCGEventRightMouseDragged;
        button = kCGMouseButtonRight;
    } else if (g_input.held == kCGMouseButtonCenter) {
        type = kCGEventOtherMouseDragged;
        button = kCGMouseButtonCenter;
    }
    CGEventRef event = CGEventCreateMouseEvent(nullptr, type, to, button);
    if (event && g_input.held >= 0)
        CGEventSetIntegerValueField(event, kCGMouseEventClickState, 1);
    post(event);
    g_input.at = to;
}

/// @p evdev is the button as the portal names it (BTN_LEFT and friends).
void mouseButton(int evdev, bool down)
{
    CGMouseButton button = kCGMouseButtonLeft;
    CGEventType type = down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
    if (evdev == 0x111) {
        button = kCGMouseButtonRight;
        type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp;
    } else if (evdev == 0x112) {
        button = kCGMouseButtonCenter;
        type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
    }
    if (down) {
        const bool again = g_input.lastButton == int(button)
            && qAbs(g_input.at.x - g_input.lastDown.x) < 3
            && qAbs(g_input.at.y - g_input.lastDown.y) < 3;
        g_input.clicks = again ? qMin(g_input.clicks + 1, 3) : 1;
        g_input.lastButton = int(button);
        g_input.lastDown = g_input.at;
        g_input.held = int(button);
    } else {
        g_input.held = -1;
    }
    CGEventRef event = CGEventCreateMouseEvent(nullptr, type, g_input.at, button);
    if (event) {
        CGEventSetIntegerValueField(event, kCGMouseEventClickState, qMax(1, g_input.clicks));
        // Exactly the modifiers this action holds, not whatever the last
        // key event left in the system's idea of the keyboard.
        CGEventSetFlags(event, g_input.flags);
    }
    post(event);
}

/// Positive @p steps move down the page (or right), as the portal counts;
/// CoreGraphics counts the other way.
void scrollWheel(uint axis, int steps)
{
    post(CGEventCreateScrollWheelEvent2(nullptr, kCGScrollEventUnitLine, 2,
                                        axis == 0 ? -steps : 0, axis == 1 ? -steps : 0, 0));
}

// ── The accessibility tree ──────────────────────────────────────────────────

/// The space callers work in: @p f screen points to one unit of it.
struct Geo {
    double f = 1;
    int w = 0;
    int h = 0;
};

/// Roles worth showing even when they carry no label of their own.
bool isInteractive(const QString &role)
{
    static const QSet<QString> roles{
        u"AXButton"_s, u"AXTextField"_s, u"AXTextArea"_s, u"AXCheckBox"_s,
        u"AXRadioButton"_s, u"AXPopUpButton"_s, u"AXMenuButton"_s, u"AXComboBox"_s,
        u"AXSlider"_s, u"AXLink"_s, u"AXMenuItem"_s, u"AXMenuBarItem"_s, u"AXTab"_s,
        u"AXDisclosureTriangle"_s, u"AXIncrementor"_s, u"AXColorWell"_s,
        u"AXSearchField"_s, u"AXSecureTextField"_s, u"AXSwitch"_s, u"AXToggle"_s,
    };
    return roles.contains(role);
}

QString clip(const QString &text, int max)
{
    const QString s = text.simplified();
    return s.size() > max ? s.left(max - 1) + u'…' : s;
}

/// One accessibility value as something plain: a string, a number, a
/// boolean, a point or a size. Elements and errors come back invalid.
QVariant axPlain(CFTypeRef value)
{
    if (!value)
        return {};
    const CFTypeID type = CFGetTypeID(value);
    if (type == CFStringGetTypeID())
        return QString::fromCFString(static_cast<CFStringRef>(value));
    if (type == CFBooleanGetTypeID())
        return bool(CFBooleanGetValue(static_cast<CFBooleanRef>(value)));
    if (type == CFNumberGetTypeID()) {
        double number = 0;
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &number);
        return number;
    }
    if (type == AXValueGetTypeID()) {
        const auto boxed = static_cast<AXValueRef>(value);
        if (AXValueGetType(boxed) == kAXValueTypeCGPoint) {
            CGPoint p{};
            if (AXValueGetValue(boxed, kAXValueTypeCGPoint, &p))
                return QPointF(p.x, p.y);
        } else if (AXValueGetType(boxed) == kAXValueTypeCGSize) {
            CGSize s{};
            if (AXValueGetValue(boxed, kAXValueTypeCGSize, &s))
                return QSizeF(s.width, s.height);
        }
    }
    return {};
}

Cf<CFTypeRef> axAttr(AXUIElementRef element, CFStringRef name)
{
    CFTypeRef value = nullptr;
    if (!element || AXUIElementCopyAttributeValue(element, name, &value) != kAXErrorSuccess)
        return {};
    return Cf<CFTypeRef>(value);
}

Element axElementAttr(AXUIElementRef element, CFStringRef name)
{
    Cf<CFTypeRef> value = axAttr(element, name);
    if (!value || CFGetTypeID(value.get()) != AXUIElementGetTypeID())
        return {};
    return Element::retained(static_cast<AXUIElementRef>(value.get()));
}

QString axString(AXUIElementRef element, CFStringRef name)
{
    return axPlain(axAttr(element, name).get()).toString();
}

Element axApp(pid_t pid)
{
    AXUIElementRef app = AXUIElementCreateApplication(pid);
    // A hung app would otherwise stall every call for the 6 s default.
    if (app)
        AXUIElementSetMessagingTimeout(app, 0.5f);
    return Element(app);
}

QList<Element> axList(AXUIElementRef element, CFStringRef name)
{
    QList<Element> out;
    const Cf<CFTypeRef> value = axAttr(element, name);
    if (!value || CFGetTypeID(value.get()) != CFArrayGetTypeID())
        return out;
    const auto array = static_cast<CFArrayRef>(value.get());
    for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
        const CFTypeRef item = CFArrayGetValueAtIndex(array, i);
        if (item && CFGetTypeID(item) == AXUIElementGetTypeID())
            out << Element::retained(static_cast<AXUIElementRef>(item));
    }
    return out;
}

/// Everything a snapshot wants to know about one element.
struct Attrs {
    bool ok = false;
    QString role, subrole, title, description, help, identifier, mark;
    QVariant value, enabled, focused, selected, expanded, position, size;
};

/// All of it in a single round trip to the app that owns the element.
Attrs axRead(AXUIElementRef element)
{
    static const CFStringRef names[] = {
        kAXRoleAttribute, kAXSubroleAttribute, kAXTitleAttribute, kAXDescriptionAttribute,
        kAXValueAttribute, kAXHelpAttribute, kAXIdentifierAttribute, kAXEnabledAttribute,
        kAXFocusedAttribute, kAXSelectedAttribute, kAXPositionAttribute, kAXSizeAttribute,
        kAXMenuItemMarkCharAttribute, kAXExpandedAttribute,
    };
    static const CFArrayRef wanted = CFArrayCreate(
        nullptr, reinterpret_cast<const void **>(const_cast<CFStringRef *>(names)),
        CFIndex(std::size(names)), &kCFTypeArrayCallBacks);
    Attrs a;
    CFArrayRef raw = nullptr;
    if (!element
        || AXUIElementCopyMultipleAttributeValues(element, wanted, 0, &raw) != kAXErrorSuccess
        || !raw)
        return a;
    const Cf<CFArrayRef> values(raw);
    if (CFArrayGetCount(raw) < CFIndex(std::size(names)))
        return a;
    const auto at = [raw](int i) { return axPlain(CFArrayGetValueAtIndex(raw, i)); };
    a.ok = true;
    a.role = at(0).toString();
    a.subrole = at(1).toString();
    a.title = at(2).toString();
    a.description = at(3).toString();
    a.value = at(4);
    a.help = at(5).toString();
    a.identifier = at(6).toString();
    a.enabled = at(7);
    a.focused = at(8);
    a.selected = at(9);
    a.position = at(10);
    a.size = at(11);
    a.mark = at(12).toString();
    a.expanded = at(13);
    return a;
}

/// A password field, whose value must never be reported. AppKit and WebKit
/// expose one as a text field with this subrole; there is no such role, but
/// it is checked too in case some app reports it.
bool isSecureField(const Attrs &a)
{
    return a.role == "AXSecureTextField"_L1 || a.subrole == "AXSecureTextField"_L1;
}

/// "on", "off" or "mixed" for a control that holds such a state, else empty.
QString toggleState(const Attrs &a)
{
    static const QSet<QString> toggles{u"AXCheckBox"_s, u"AXSwitch"_s, u"AXToggle"_s,
                                       u"AXRadioButton"_s, u"AXMenuItem"_s};
    if (!toggles.contains(a.role))
        return {};
    if (a.role == "AXMenuItem"_L1)
        return a.mark.isEmpty() ? QString() : u"on"_s;
    if (a.value.typeId() != QMetaType::Bool && a.value.typeId() != QMetaType::Double)
        return {};
    const double v = a.value.toDouble();
    return v == 1 ? u"on"_s : v == 0 ? u"off"_s : v == 2 ? u"mixed"_s : QString();
}

/// One element as a snapshot item, or nothing when it is not something a
/// person could name or act on, or lies off the main display.
std::optional<QJsonObject> axItem(const Attrs &a, const Geo &geo, bool keepAll)
{
    if (!a.ok)
        return std::nullopt;
    QString label = clip(!a.title.isEmpty() ? a.title
                         : !a.description.isEmpty() ? a.description : a.help, 80);
    const QString state = toggleState(a);
    QString value;
    if (!isSecureField(a) && a.role != "AXHeading"_L1 && state.isEmpty()) {
        if (a.value.typeId() == QMetaType::QString)
            value = clip(a.value.toString(), 120);
        else if (a.value.typeId() == QMetaType::Double)
            value = QString::number(a.value.toDouble());
    }
    const bool interactive = isInteractive(a.role);
    if (interactive && label.isEmpty()) {
        // Icon-only controls keep their name in the identifier or subrole.
        static const QRegularExpression generated(u"^_NS:\\d+$"_s);
        label = clip(a.identifier, 80);
        if (generated.match(label).hasMatch())
            label.clear();
        if (label.isEmpty() && a.subrole.startsWith("AX"_L1))
            label = clip(a.subrole.mid(2), 80);
    }
    if (!keepAll && !interactive && label.isEmpty()
        && !(a.role == "AXStaticText"_L1 && !value.isEmpty()))
        return std::nullopt;
    if (a.position.typeId() != QMetaType::QPointF || a.size.typeId() != QMetaType::QSizeF)
        return std::nullopt;
    const QPointF pos = a.position.toPointF();
    const QSizeF size = a.size.toSizeF();
    if (size.width() <= 0 || size.height() <= 0)
        return std::nullopt;
    const double cx = (pos.x() + size.width() / 2) / geo.f;
    const double cy = (pos.y() + size.height() / 2) / geo.f;
    if (cx < 0 || cy < 0 || cx > geo.w || cy > geo.h)
        return std::nullopt;
    QJsonObject item{
        {"role"_L1, a.role.startsWith("AX"_L1) ? a.role.mid(2) : a.role},
        {"x"_L1, qRound(cx)},
        {"y"_L1, qRound(cy)},
        {"w"_L1, qRound(size.width() / geo.f)},
        {"h"_L1, qRound(size.height() / geo.f)},
    };
    if (!label.isEmpty())
        item["label"_L1] = label;
    if (!value.isEmpty() && value != label)
        item["value"_L1] = value;
    if (!state.isEmpty())
        item["state"_L1] = state;
    if (a.selected.typeId() == QMetaType::Bool && a.selected.toBool() && state.isEmpty())
        item["selected"_L1] = true;
    if (a.expanded.typeId() == QMetaType::Bool && a.expanded.toBool())
        item["expanded"_L1] = true;
    if (a.enabled.typeId() == QMetaType::Bool && !a.enabled.toBool())
        item["disabled"_L1] = true;
    if (a.focused.typeId() == QMetaType::Bool && a.focused.toBool())
        item["focused"_L1] = true;
    return item;
}

struct Walk {
    QJsonArray items;
    int limit = 150;
    int visited = 0;
    QElapsedTimer clock;
    qint64 deadline = 0; ///< on @c clock
    QString truncated;
    QSet<QString> seen;

    bool full() const { return items.size() >= limit; }
    bool late() const { return clock.elapsed() > deadline; }
};

/// Walk one subtree depth-first into items. Submenus are not entered: an
/// open one is a window of its own and is walked as one, and a closed one
/// only holds stale positions.
void axWalk(const Element &root, const Geo &geo, Walk *ctx, const QJsonObject &extra)
{
    QList<std::pair<Element, int>> stack{{root, 0}};
    while (!stack.isEmpty()) {
        if (ctx->full()) {
            ctx->truncated = u"limit"_s;
            return;
        }
        if (ctx->late()) {
            ctx->truncated = u"time"_s;
            return;
        }
        const auto [element, depth] = stack.takeLast();
        const Attrs a = axRead(element.get());
        if (!a.ok)
            continue;
        ++ctx->visited;
        std::optional<QJsonObject> item =
            a.role == "AXWindow"_L1 ? std::nullopt : axItem(a, geo, false);
        if (item && item->value("role"_L1).toString() == "MenuItem"_L1) {
            // Separators have no title; option-key alternates sit exactly
            // on the item they replace.
            const QString spot = u"%1,%2,%3"_s.arg((*item)["x"_L1].toInt())
                                     .arg((*item)["y"_L1].toInt()).arg((*item)["w"_L1].toInt());
            if (!item->contains("label"_L1) || ctx->seen.contains(spot))
                item.reset();
            else
                ctx->seen.insert(spot);
        }
        if (item) {
            for (auto it = extra.begin(); it != extra.end(); ++it)
                (*item)[it.key()] = it.value();
            ctx->items.append(*item);
        }
        if (depth >= 40 || (a.role == "AXMenuItem"_L1 && depth > 0))
            continue;
        const QList<Element> kids = axList(element.get(), kAXChildrenAttribute);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            stack.append({*it, depth + 1});
    }
}

/// The accessibility root behind one on-screen window: the app's window
/// with the same frame, or, for menus, popovers and panels an app does not
/// list as windows, whatever the window's top hit-tests to, climbed to its
/// menu or top-level element.
Element axWindowRoot(const Element &app, const QRectF &frame)
{
    for (const Element &window : axList(app.get(), kAXWindowsAttribute)) {
        const QVariant p = axPlain(axAttr(window.get(), kAXPositionAttribute).get());
        const QVariant s = axPlain(axAttr(window.get(), kAXSizeAttribute).get());
        if (p.typeId() != QMetaType::QPointF || s.typeId() != QMetaType::QSizeF)
            continue;
        const QRectF own(p.toPointF(), s.toSizeF());
        if (qAbs(own.x() - frame.x()) <= 2 && qAbs(own.y() - frame.y()) <= 2
            && qAbs(own.width() - frame.width()) <= 2
            && qAbs(own.height() - frame.height()) <= 2)
            return window;
    }
    AXUIElementRef hit = nullptr;
    const float cx = float(frame.x() + frame.width() / 2);
    const float cy = float(frame.y() + qMin(frame.height() / 2, 24.0));
    if (AXUIElementCopyElementAtPosition(app.get(), cx, cy, &hit) != kAXErrorSuccess || !hit)
        return {};
    Element element(hit);
    for (int depth = 0; depth < 40; ++depth) {
        const QString role = axString(element.get(), kAXRoleAttribute);
        if (role == "AXMenu"_L1 || role == "AXWindow"_L1 || role == "AXSheet"_L1)
            return element;
        Element parent = axElementAttr(element.get(), kAXParentAttribute);
        if (!parent || axString(parent.get(), kAXRoleAttribute) == "AXApplication"_L1)
            return element;
        element = parent;
    }
    return element;
}

struct RunningApp {
    pid_t pid = 0;
    QString name;
    bool regular = false; ///< an app with a Dock icon and windows of its own
};

QList<RunningApp> runningApps()
{
    QList<RunningApp> out;
    for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications) {
        out.append({app.processIdentifier, QString::fromNSString(app.localizedName ?: @""),
                    app.activationPolicy == NSApplicationActivationPolicyRegular});
    }
    return out;
}

/// The system's interface around the frontmost window: the frontmost app's
/// menu bar, every menu bar extra, and the controls inside every other
/// on-screen window that is not an ordinary app window (open menus, Control
/// Centre and its popovers, Notification Centre, floating panels). Ordinary
/// windows of other apps are listed, not walked.
QJsonObject systemInterface(pid_t frontPid, const QList<RunningApp> &apps, const Geo &geo,
                            Walk *ctx)
{
    // kCGStatusWindowLevel: the windows menu bar extras are drawn in.
    constexpr int kStatusLayer = 25;
    // Owners that are scenery: walking them yields nothing to act on, or
    // (the Dock) far more than anyone asked for.
    static const QSet<QString> scenery{u"Window Server"_s, u"Dock"_s, u"Wallpaper"_s,
                                       u"WindowManager"_s};
    QHash<pid_t, RunningApp> byPid;
    for (const RunningApp &app : apps)
        byPid.insert(app.pid, app);

    QJsonArray menuBar;
    if (frontPid > 0) {
        const Element bar = axElementAttr(axApp(frontPid).get(), kAXMenuBarAttribute);
        for (const Element &entry : axList(bar.get(), kAXChildrenAttribute)) {
            const auto item = axItem(axRead(entry.get()), geo, true);
            if (item && item->contains("label"_L1)) {
                menuBar.append(QJsonObject{{"label"_L1, (*item)["label"_L1]},
                                           {"x"_L1, (*item)["x"_L1]},
                                           {"y"_L1, (*item)["y"_L1]}});
            }
        }
    }

    // Menu extras hang off each owner's extras menu bar, or are grouped
    // under one agent; either way the clickable thing is the menu bar item,
    // sometimes a level or two down.
    QList<QJsonObject> extras;
    for (const RunningApp &app : apps) {
        if (ctx->late())
            break;
        const Element bar = axElementAttr(axApp(app.pid).get(), CFSTR("AXExtrasMenuBar"));
        if (!bar)
            continue;
        QList<std::pair<Element, int>> queue;
        for (const Element &kid : axList(bar.get(), kAXChildrenAttribute))
            queue.append({kid, 0});
        while (!queue.isEmpty()) {
            const auto [element, depth] = queue.takeFirst();
            const Attrs a = axRead(element.get());
            if (!a.ok)
                continue;
            if (a.role == "AXMenuBarItem"_L1) {
                if (auto item = axItem(a, geo, true)) {
                    // Third-party extras often carry no name of their own.
                    const QString label = item->value("label"_L1).toString();
                    if (label.isEmpty() || label == "MenuExtra"_L1)
                        (*item)["label"_L1] = app.name;
                    (*item)["app"_L1] = app.name;
                    item->remove("role"_L1);
                    extras.append(*item);
                }
            } else if (depth < 3) {
                for (const Element &kid : axList(element.get(), kAXChildrenAttribute))
                    queue.append({kid, depth + 1});
            }
        }
    }
    std::sort(extras.begin(), extras.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a["x"_L1].toInt() < b["x"_L1].toInt();
    });
    QJsonArray extrasOut;
    for (const QJsonObject &extra : extras)
        extrasOut.append(extra);

    // Every other window on screen, frontmost first.
    QJsonArray windows;
    NSArray *list = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    bool frontSkipped = false;
    QList<Element> roots;
    for (NSDictionary *window in list) {
        NSDictionary *bounds = window[(__bridge NSString *)kCGWindowBounds];
        const QRectF frame([bounds[@"X"] doubleValue], [bounds[@"Y"] doubleValue],
                           [bounds[@"Width"] doubleValue], [bounds[@"Height"] doubleValue]);
        const QString owner = QString::fromNSString(
            window[(__bridge NSString *)kCGWindowOwnerName] ?: @"");
        const pid_t pid = [window[(__bridge NSString *)kCGWindowOwnerPID] intValue];
        const int layer = [window[(__bridge NSString *)kCGWindowLayer] intValue];
        NSNumber *alpha = window[(__bridge NSString *)kCGWindowAlpha];
        if (frame.width() < 8 || frame.height() < 8 || scenery.contains(owner))
            continue;
        // Status items are windows too; the extras above already have them.
        if (layer == kStatusLayer || (alpha && alpha.doubleValue == 0))
            continue;
        const double cx = frame.center().x() / geo.f;
        const double cy = frame.center().y() / geo.f;
        if (cx < 0 || cy < 0 || cx > geo.w || cy > geo.h)
            continue;
        const RunningApp info = byPid.value(pid);
        // The frontmost app's own window is the main snapshot's job.
        if (pid == frontPid && layer == 0 && !frontSkipped) {
            frontSkipped = true;
            continue;
        }
        const QString appName = info.name.isEmpty() ? owner : info.name;
        QJsonObject entry{
            {"app"_L1, appName},
            {"x"_L1, qRound(frame.x() / geo.f)},
            {"y"_L1, qRound(frame.y() / geo.f)},
            {"w"_L1, qRound(frame.width() / geo.f)},
            {"h"_L1, qRound(frame.height() / geo.f)},
        };
        const QString title = clip(
            QString::fromNSString(window[(__bridge NSString *)kCGWindowName] ?: @""), 80);
        if (!title.isEmpty())
            entry["title"_L1] = title;
        if (layer)
            entry["layer"_L1] = layer;
        // Ordinary app windows and desktop widgets (negative layers) are
        // listed only: they are big, and seldom what was missed.
        const bool listOnly = (info.regular && layer == 0) || layer < 0;
        if (!listOnly && !ctx->late() && !ctx->full()) {
            const Element root = axWindowRoot(axApp(pid), frame);
            bool walked = !root;
            for (const Element &seen : roots)
                walked = walked || CFEqual(seen.get(), root.get());
            if (!walked) {
                roots.append(root);
                const qsizetype before = ctx->items.size();
                QJsonObject tag{{"app"_L1, appName}};
                if (!title.isEmpty())
                    tag["window"_L1] = title;
                axWalk(root, geo, ctx, tag);
                entry["walked"_L1] = int(ctx->items.size() - before);
            }
        }
        windows.append(entry);
    }

    QJsonObject out{
        {"menuBar"_L1, menuBar},
        {"extras"_L1, extrasOut},
        {"windows"_L1, windows},
        {"elements"_L1, ctx->items},
    };
    if (!ctx->truncated.isEmpty())
        out["truncated"_L1] = ctx->truncated;
    return out;
}

/// The frontmost window's tree, flattened to what a person could name or
/// act on, each with the centre point to click. Runs off the main thread:
/// every element is a round trip to another app.
QJsonObject readSnapshot(const Geo &geo, bool everything, int limit, int budgetMs)
{
    @autoreleasepool {
        QElapsedTimer clock;
        clock.start();
        NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication;
        if (!front)
            return {{"error"_L1, u"no frontmost application"_s}};
        const pid_t frontPid = front.processIdentifier;
        const QList<RunningApp> apps = runningApps();
        QJsonArray names;
        for (const RunningApp &app : apps) {
            // The apps a person could switch to, not every agent running.
            if (app.regular && !app.name.isEmpty())
                names.append(app.name);
        }
        QJsonObject out{
            {"app"_L1, QString::fromNSString(front.localizedName ?: @"")},
            {"window"_L1, QString()},
            {"apps"_L1, names},
        };
        if (everything) {
            // The system's interface first: it is small, and it is what the
            // frontmost window's tree can never show. It gets at most half
            // the time, so the window still gets read.
            Walk system;
            system.limit = limit;
            system.clock = clock;
            system.deadline = budgetMs / 2;
            out["system"_L1] = systemInterface(frontPid, apps, geo, &system);
        }
        const Element app = axApp(frontPid);
        Element window = axElementAttr(app.get(), kAXMainWindowAttribute);
        if (!window)
            window = axElementAttr(app.get(), kAXFocusedWindowAttribute);
        if (!window) {
            const QList<Element> all = axList(app.get(), kAXWindowsAttribute);
            if (!all.isEmpty())
                window = all.first();
        }
        Walk walk;
        walk.limit = limit;
        walk.clock = clock;
        walk.deadline = budgetMs;
        if (!window) {
            out["note"_L1] = u"the frontmost application has no window"_s;
        } else {
            out["window"_L1] = clip(axString(window.get(), kAXTitleAttribute), 120);
            axWalk(window, geo, &walk, {});
        }
        out["elements"_L1] = walk.items;
        out["total"_L1] = walk.visited;
        if (!walk.truncated.isEmpty())
            out["truncated"_L1] = walk.truncated;
        return out;
    }
}

struct Hit {
    Element element;
    QJsonObject item;
    QString app;
};

/// One element as a compact item with the app that owns it, or nothing
/// when it no longer answers.
std::optional<QJsonObject> describe(const Element &element, const Geo &geo, const QString &app)
{
    const Attrs a = axRead(element.get());
    if (!a.ok || a.role.isEmpty())
        return std::nullopt;
    QJsonObject item = axItem(a, geo, true).value_or(
        QJsonObject{{"role"_L1, a.role.startsWith("AX"_L1) ? a.role.mid(2) : a.role}});
    if (!app.isEmpty())
        item["app"_L1] = app;
    return item;
}

/// The element under a point of the screen, climbed to the nearest control
/// when the hit is a label or image inside one.
std::optional<Hit> hitTest(const QPointF &point, const Geo &geo)
{
    @autoreleasepool {
        const Element wide(AXUIElementCreateSystemWide());
        if (!wide)
            return std::nullopt;
        AXUIElementSetMessagingTimeout(wide.get(), 0.5f);
        AXUIElementRef raw = nullptr;
        if (AXUIElementCopyElementAtPosition(wide.get(), float(point.x()), float(point.y()), &raw)
                != kAXErrorSuccess || !raw)
            return std::nullopt;
        Element element(raw);
        Element hit = element;
        for (int depth = 0; depth < 4; ++depth) {
            if (isInteractive(axString(element.get(), kAXRoleAttribute))) {
                hit = element;
                break;
            }
            Element parent = axElementAttr(element.get(), kAXParentAttribute);
            if (!parent)
                break;
            element = parent;
        }
        QString app;
        pid_t pid = 0;
        if (AXUIElementGetPid(hit.get(), &pid) == kAXErrorSuccess) {
            NSRunningApplication *running =
                [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
            app = QString::fromNSString(running.localizedName ?: @"");
        }
        const auto item = describe(hit, geo, app);
        if (!item)
            return std::nullopt;
        return Hit{hit, *item, app};
    }
}

} // namespace

// ── ScreenControl ───────────────────────────────────────────────────────────

ScreenControl::~ScreenControl() = default;

bool ScreenControl::available() const
{
    // Needs the app's own screens; the background service has none.
    return qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr;
}

void ScreenControl::notify(const QString &method, const QVariantList &args)
{
    // The first argument of each is the portal's options map.
    if (!m_ready)
        return;
    if (method == "NotifyPointerMotionAbsolute"_L1 && args.size() >= 4)
        mouseMove(CGPointMake(args.at(2).toDouble(), args.at(3).toDouble()));
    else if (method == "NotifyPointerButton"_L1 && args.size() >= 3)
        mouseButton(args.at(1).toInt(), args.at(2).toUInt() != 0);
    else if (method == "NotifyKeyboardKeysym"_L1 && args.size() >= 3)
        keyEvent(args.at(1).toInt(), args.at(2).toUInt() != 0);
    else if (method == "NotifyPointerAxisDiscrete"_L1 && args.size() >= 3)
        scrollWheel(args.at(1).toUInt(), args.at(2).toInt());
}

void ScreenControl::ensureSession(std::function<void(const QString &)> then)
{
    // There is no session to set up: the main display is the "stream", and
    // its points are the coordinates events are posted in.
    const CGRect bounds = CGDisplayBounds(CGMainDisplayID());
    m_streamSize = QSize(qRound(bounds.size.width), qRound(bounds.size.height));
    m_streamPos = {};
    m_stream = 1;
    m_session = u"macos"_s;
    g_input = Input{};
    g_input.at = pointerNow();
    g_input.layout = readLayout();
    // Unlike Wayland, the Mac says where the pointer really is.
    m_cursor = QPointF(g_input.at.x, g_input.at.y);

    const bool trusted = AXIsProcessTrusted();
    if (trusted != m_ready) {
        m_ready = trusted;
        emit changed();
    }
    if (m_action == "screenshot"_L1) {
        if (!CGPreflightScreenCaptureAccess()) {
            // Raises the system's own request the first time; after that
            // only the person can change the answer.
            CGRequestScreenCaptureAccess();
            then(u"macOS has not allowed Talos to record the screen. Allow it under System "
                 "Settings › Privacy & Security › Screen & System Audio Recording, then quit "
                 "and reopen Talos."_s);
            return;
        }
        then({});
        return;
    }
    if (!trusted) {
        NSDictionary *prompt = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES};
        AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)prompt);
        then(u"macOS has not allowed Talos to control this Mac. Allow it under System "
             "Settings › Privacy & Security › Accessibility, then try again."_s);
        return;
    }
    then({});
}

void ScreenControl::screenshot(const QJsonObject &params, DeviceExec::Done done)
{
    const auto dir = std::make_shared<QTemporaryDir>();
    if (!dir->isValid()) {
        done(CommandOutcome::fail(u"computer screenshot: no temporary folder to capture into."_s));
        return;
    }
    const QString file = dir->filePath(u"shot.png"_s);
    auto *tool = new QProcess(this);
    connect(tool, &QProcess::finished, this,
            [this, tool, dir, file, params, done](int code, QProcess::ExitStatus status) {
        tool->deleteLater();
        const QImage shot(file);
        if (status != QProcess::NormalExit || code != 0 || shot.isNull()) {
            done(CommandOutcome::fail(
                u"computer screenshot: macOS returned no picture. Check that Talos is allowed "
                "under System Settings › Privacy & Security › Screen & System Audio "
                "Recording."_s));
            return;
        }
        replyWithShot(shot, params, done);
    });
    connect(tool, &QProcess::errorOccurred, this, [tool, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        tool->deleteLater();
        done(CommandOutcome::fail(u"computer screenshot: could not run screencapture."_s));
    });
    QTimer::singleShot(20'000, tool, [tool] { tool->kill(); });
    // -x no shutter sound, -m the main display only, -C with the pointer.
    tool->start(u"/usr/sbin/screencapture"_s,
                {u"-x"_s, u"-m"_s, u"-C"_s, u"-t"_s, u"png"_s, file});
}

bool ScreenControl::canSnapshot()
{
    return true;
}

void ScreenControl::snapshot(const QJsonObject &params, DeviceExec::Done done)
{
    const QString scope = params["scope"_L1].toString(u"front"_s).toLower();
    if (scope != "front"_L1 && scope != "all"_L1) {
        done(CommandOutcome::fail(u"computer snapshot: unknown snapshot scope \"%1\" "
                                  "(front or all)"_s.arg(scope)));
        return;
    }
    double f = 1;
    const QSize space = spaceFor(m_streamSize, params["maxEdge"_L1].toInt(1280), &f);
    const Geo geo{f, space.width(), space.height()};
    const int asked = params["limit"_L1].toInt();
    const int limit = qBound(1, asked ? asked : 150, 400);
    const int budgetAsked = params["budgetMs"_L1].toInt();
    const int budgetMs = qBound(500, budgetAsked ? budgetAsked : 8000, 20'000);
    const QJsonObject base = baseReply(space);

    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [watcher, base, done] {
        const QJsonObject read = watcher->result();
        watcher->deleteLater();
        if (read.contains("error"_L1)) {
            done(CommandOutcome::fail(u"computer snapshot: %1"_s
                                          .arg(read["error"_L1].toString())));
            return;
        }
        QJsonObject data = base;
        for (auto it = read.begin(); it != read.end(); ++it)
            data[it.key()] = it.value();
        done(CommandOutcome::done(data));
    });
    watcher->setFuture(QtConcurrent::run([geo, scope, limit, budgetMs] {
        return readSnapshot(geo, scope == "all"_L1, limit, budgetMs);
    }));
}

std::function<void(QJsonObject *)> ScreenControl::clickProbe(const QPointF &point, double factor)
{
    const Geo geo{factor, qRound(m_streamSize.width() / factor),
                  qRound(m_streamSize.height() / factor)};
    const std::optional<Hit> before = hitTest(point, geo);
    return [before, point, geo](QJsonObject *out) {
        if (before) {
            // The same element again: a toggle reports its new state.
            out->insert("target"_L1, before->item);
            if (const auto again = describe(before->element, geo, before->app))
                out->insert("targetAfter"_L1, *again);
            else
                out->insert("targetGone"_L1, true);
        }
        // And whatever sits there now: a menu that opened, a sheet that
        // replaced the control.
        if (const auto now = hitTest(point, geo))
            out->insert("under"_L1, now->item);
    };
}

void ScreenControl::revoke()
{
    // Only the person can withdraw what they granted: show them where.
    QDesktopServices::openUrl(QUrl(
        u"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"_s));
    const bool trusted = AXIsProcessTrusted();
    if (trusted != m_ready) {
        m_ready = trusted;
        emit changed();
    }
}

} // namespace talos
