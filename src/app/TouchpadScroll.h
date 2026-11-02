// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QPointingDevice>
#include <QTimer>
#include <QVarLengthArray>

class QQuickItem;
class QWheelEvent;
class QWindow;

namespace talos {

/// Kinetic touchpad scrolling for Qt Quick on the Linux desktop.
///
/// Flickable follows a touchpad only while the fingers move and stops dead
/// when they lift: Qt leaves the glide to the system, which only macOS
/// provides. Under X11 it is worse: touchpad scrolling arrives as plain
/// wheel steps, without the begin and end of the gesture, so Flickable
/// treats it like a mouse wheel and does not follow the fingers at all.
///
/// Installed on the application, this
///  - turns X11 touchpad scrolling into a gesture (begin, pixel updates,
///    end after a short quiet spell), which Flickable then follows 1:1,
///    as it already does under Wayland;
///  - measures how fast the fingers were moving when they lifted and hands
///    that to the Flickable that was being scrolled, as Flickable::flick(),
///    so the glide uses Qt's own physics and stops on a click or a new
///    swipe like any flick.
///
/// Mouse wheels pass through untouched.
class TouchpadScroll : public QObject
{
    Q_OBJECT
public:
    explicit TouchpadScroll(QObject *parent = nullptr);

    /// Worth installing here: a Linux desktop session (Wayland or X11),
    /// unless TALOS_KINETIC_SCROLL=0 turns it off.
    static bool wanted();

    bool eventFilter(QObject *watched, QEvent *event) override;

    /// X11: no touchpad event for this long means the fingers have lifted.
    static constexpr int QuietMs = 50;
    /// Fingers resting longer than this before lifting: no glide.
    static constexpr int RestMs = 60;
    /// Slower than this (px/s) at release: precise scrolling, no glide.
    static constexpr qreal MinSpeed = 150;
    /// The glide covers about what the fingers would in this long (s).
    static constexpr qreal GlideTime = 0.32;

private:
    struct Sample
    {
        qint64 time;
        QPointF delta;
    };

    bool windowWheel(QWindow *window, QWheelEvent *event);
    bool synthesize(QWindow *window, QWheelEvent *event);
    void stamp(QWheelEvent *event);
    void start();
    void sample(qint64 time, QPointF delta);
    void finishSynthesized(bool withGlide = true);
    void reset();
    /// The speed (px/s) the fingers had at time `end`, zero if they had
    /// stopped or were moving too slowly to glide.
    QPointF releaseVelocity(qint64 end) const;
    QQuickItem *movingTarget() const;
    void glide(QQuickItem *target, QPointF velocity);
    void send(QWindow *window, QEvent *event);

    bool m_active = false;
    bool m_synthesized = false; // X11: the gesture is ours, not the platform's
    bool m_sending = false;
    QVarLengthArray<Sample, 16> m_samples;
    QPointer<QQuickItem> m_flickable;
    QVarLengthArray<QPointer<QQuickItem>, 4> m_synced; // synchronousDrag set for the swipe
    qint64 m_lastTime = 0;

    // X11 gesture state, for the end it never sends.
    QTimer m_quiet;
    QPointer<QWindow> m_window;
    QPointer<const QPointingDevice> m_device;
    QPointF m_position;
    QPointF m_globalPosition;
    Qt::KeyboardModifiers m_modifiers;
    bool m_inverted = false;
    QPointF m_remainder;
};

} // namespace talos
