// SPDX-License-Identifier: GPL-2.0-only
#include "app/TouchpadScroll.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointingDevice>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedValueRollback>
#include <QWheelEvent>
#include <QWindow>

#include <cmath>
#include <memory>

namespace talos {

namespace {
/// Release speed is measured over the last this many milliseconds.
constexpr qint64 VelocityWindowMs = 60;
/// Faster than this (px/s) is a measuring glitch, not fingers.
constexpr qreal MaxSpeed = 8000;
/// libinput measures touchpad scrolling in pixels, and X11 passes it on as
/// wheel angle at 15 px to a 120° step: 8 per pixel.
constexpr qreal AnglePerPixel = 8;
} // namespace

TouchpadScroll::TouchpadScroll(QObject *parent)
    : QObject(parent)
{
    m_quiet.setSingleShot(true);
    m_quiet.setInterval(QuietMs);
    connect(&m_quiet, &QTimer::timeout, this, [this] { finishSynthesized(); });
}

bool TouchpadScroll::wanted()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (qEnvironmentVariable("TALOS_KINETIC_SCROLL") == u"0")
        return false;
    const QString platform = QGuiApplication::platformName();
    return platform.startsWith(u"wayland") || platform == u"xcb";
#else
    return false;
#endif
}

bool TouchpadScroll::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Wheel)
        return false;
    auto *wheel = static_cast<QWheelEvent *>(event);
    if (watched->isWindowType()) {
        // Qt Quick windows only: widget windows scroll in their own way.
        auto *window = qobject_cast<QQuickWindow *>(watched);
        return window && !m_sending ? windowWheel(window, wheel) : false;
    }

    // Qt Quick hands a wheel event to the items under the pointer until one
    // takes it, so the last Flickable to see an update is the one scrolling.
    if (m_active && wheel->phase() == Qt::ScrollUpdate && !wheel->pixelDelta().isNull()
        && watched->inherits("QQuickFlickable")) {
        auto *flickable = qobject_cast<QQuickItem *>(watched);
        m_flickable = flickable;
        // Without this a Flickable treats the start of the swipe as a touch
        // drag's slop and drops it: a short quick swipe barely moves. Only
        // for the swipe, so touch dragging keeps its smooth start.
        if (flickable && !flickable->property("synchronousDrag").toBool()) {
            flickable->setProperty("synchronousDrag", true);
            m_synced.append(flickable);
        }
    }
    return false;
}

bool TouchpadScroll::windowWheel(QWindow *window, QWheelEvent *event)
{
    switch (event->phase()) {
    case Qt::NoScrollPhase: {
        const QPointingDevice *device = event->pointingDevice();
        if (device && device->type() == QInputDevice::DeviceType::TouchPad)
            return synthesize(window, event);
        // A mouse wheel in the middle of it: the touchpad is done.
        finishSynthesized(false);
        return false;
    }
    case Qt::ScrollBegin:
        finishSynthesized(false);
        start();
        // Wayland sends the begin with the first step's time, and Flickable
        // drops a step that seems to take no time.
        if (event->timestamp() > 1)
            event->setTimestamp(event->timestamp() - 1);
        m_lastTime = qint64(event->timestamp());
        return false;
    case Qt::ScrollUpdate:
        if (m_active && !m_synthesized) {
            stamp(event);
            sample(m_lastTime, event->pixelDelta().isNull()
                                           ? QPointF(event->angleDelta()) / AnglePerPixel
                                           : QPointF(event->pixelDelta()));
        }
        return false;
    case Qt::ScrollEnd: {
        if (!m_active || m_synthesized)
            return false;
        const QPointF velocity = releaseVelocity(event->timestamp());
        const QPointer<QQuickItem> target = velocity.isNull() ? nullptr : movingTarget();
        reset();
        if (!target)
            return false;
        // End the gesture first: that settles the Flickable, which would
        // stop a glide begun before it.
        const std::unique_ptr<QEvent> end(event->clone());
        send(window, end.get());
        event->setAccepted(end->isAccepted());
        if (target)
            glide(target, velocity);
        return true;
    }
    case Qt::ScrollMomentum: // the system glides by itself (macOS)
        return false;
    }
    return false;
}

bool TouchpadScroll::synthesize(QWindow *window, QWheelEvent *event)
{
    if (m_synthesized && window != m_window)
        finishSynthesized(false);
    if (!m_synthesized) {
        start();
        m_synthesized = true;
        m_window = window;
        QWheelEvent begin(event->position(), event->globalPosition(), QPoint(), QPoint(),
                          Qt::NoButton, event->modifiers(), Qt::ScrollBegin, event->inverted(),
                          Qt::MouseEventNotSynthesized, event->pointingDevice());
        // A moment before the first step, which Flickable would otherwise
        // drop as taking no time.
        begin.setTimestamp(event->timestamp() > 1 ? event->timestamp() - 1 : event->timestamp());
        m_lastTime = qint64(begin.timestamp());
        send(window, &begin);
    }
    stamp(event);
    m_device = event->pointingDevice();
    m_position = event->position();
    m_globalPosition = event->globalPosition();
    m_modifiers = event->modifiers();
    m_inverted = event->inverted();
    m_quiet.start();
    event->accept();

    // Whole pixels for Flickable; the fractions carry over to the next event.
    const QPointF exact = m_remainder
        + (event->pixelDelta().isNull() ? QPointF(event->angleDelta()) / AnglePerPixel
                                        : QPointF(event->pixelDelta()));
    const QPoint pixels = exact.toPoint();
    m_remainder = exact - pixels;
    if (pixels.isNull())
        return true;

    // Qt Quick drops an update without an angle as a duplicate.
    const QPoint angle = event->angleDelta().isNull() ? pixels * int(AnglePerPixel)
                                                      : event->angleDelta();
    QWheelEvent update(event->position(), event->globalPosition(), pixels, angle,
                       event->buttons(), event->modifiers(), Qt::ScrollUpdate, event->inverted(),
                       Qt::MouseEventSynthesizedBySystem, event->pointingDevice());
    update.setTimestamp(event->timestamp());
    send(window, &update);
    event->setAccepted(update.isAccepted());
    sample(m_lastTime, pixels);
    return true;
}

void TouchpadScroll::finishSynthesized(bool withGlide)
{
    m_quiet.stop();
    if (!m_synthesized)
        return;
    const QPointF velocity = withGlide ? releaseVelocity(m_lastTime) : QPointF();
    const QPointer<QQuickItem> target = velocity.isNull() ? nullptr : movingTarget();
    const QPointer<QWindow> window = m_window;
    QWheelEvent end(m_position, m_globalPosition, QPoint(), QPoint(), Qt::NoButton, m_modifiers,
                    Qt::ScrollEnd, m_inverted, Qt::MouseEventNotSynthesized,
                    m_device ? m_device.get() : QPointingDevice::primaryPointingDevice());
    end.setTimestamp(ulong(m_lastTime + QuietMs));
    reset();
    if (window)
        send(window, &end);
    if (target)
        glide(target, velocity);
}

void TouchpadScroll::stamp(QWheelEvent *event)
{
    // Strictly later than the step before: Flickable drops the movement of
    // a step that arrives in the same millisecond.
    if (qint64(event->timestamp()) <= m_lastTime)
        event->setTimestamp(ulong(m_lastTime + 1));
    m_lastTime = qint64(event->timestamp());
}

void TouchpadScroll::start()
{
    reset();
    m_active = true;
}

void TouchpadScroll::reset()
{
    m_quiet.stop();
    m_active = false;
    m_synthesized = false;
    m_samples.clear();
    m_flickable = nullptr;
    for (const QPointer<QQuickItem> &flickable : std::as_const(m_synced)) {
        if (flickable)
            flickable->setProperty("synchronousDrag", false);
    }
    m_synced.clear();
    m_window = nullptr;
    m_remainder = QPointF();
}

void TouchpadScroll::sample(qint64 time, QPointF delta)
{
    if (delta.isNull())
        return;
    if (m_samples.size() == m_samples.capacity())
        m_samples.remove(0);
    m_samples.append({time, delta});
}

QPointF TouchpadScroll::releaseVelocity(qint64 end) const
{
    const qsizetype n = m_samples.size();
    if (n < 2)
        return {};
    const Sample &last = m_samples.last();
    if (end - last.time > RestMs)
        return {}; // the fingers stopped before they lifted

    // Each sample moved between the one before it and itself.
    qsizetype k = n - 2;
    while (k > 0 && last.time - m_samples[k - 1].time <= VelocityWindowMs)
        --k;
    const qint64 span = last.time - m_samples[k].time;
    if (span <= 0)
        return {};
    QPointF moved;
    for (qsizetype i = k + 1; i < n; ++i)
        moved += m_samples[i].delta;
    QPointF velocity = moved * (1000.0 / qreal(span));
    const qreal speed = std::hypot(velocity.x(), velocity.y());
    if (speed < MinSpeed)
        return {};
    if (speed > MaxSpeed)
        velocity *= MaxSpeed / speed;
    return velocity;
}

QQuickItem *TouchpadScroll::movingTarget() const
{
    // Moving: the gesture did scroll it, rather than finding it at its end.
    if (m_flickable && m_flickable->property("moving").toBool()
        && m_flickable->property("interactive").toBool())
        return m_flickable;
    return nullptr;
}

void TouchpadScroll::glide(QQuickItem *target, QPointF velocity)
{
    const qreal speed = std::hypot(velocity.x(), velocity.y());
    if (speed <= 0)
        return;
    // Flickable slows at a constant rate a, travelling v²/2a. Starting at
    // the fingers' speed v and covering v × GlideTime takes a = v / 2·GlideTime:
    // a quick swipe goes far, a gentle one barely drifts. Flickable reads
    // both settings only as a flick starts, so they go straight back to
    // what touch flicking uses.
    const QVariant deceleration = target->property("flickDeceleration");
    const QVariant maxVelocity = target->property("maximumFlickVelocity");
    target->setProperty("flickDeceleration", speed / (2 * GlideTime));
    if (maxVelocity.toReal() < speed)
        target->setProperty("maximumFlickVelocity", speed);
    QMetaObject::invokeMethod(target, "flick", Q_ARG(qreal, velocity.x()),
                              Q_ARG(qreal, velocity.y()));
    target->setProperty("flickDeceleration", deceleration);
    target->setProperty("maximumFlickVelocity", maxVelocity);
}

void TouchpadScroll::send(QWindow *window, QEvent *event)
{
    const QScopedValueRollback guard(m_sending, true);
    QCoreApplication::sendEvent(window, event);
}

} // namespace talos
