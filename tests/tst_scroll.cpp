// SPDX-License-Identifier: GPL-2.0-only
// Touchpad scrolling against a real Flickable: Wayland-style gestures (with
// begin and end), X11-style ones (bare wheel steps from a touchpad), and a
// mouse wheel, which must be left alone.
#include "app/TouchpadScroll.h"

#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>

#include <memory>

using talos::TouchpadScroll;
using namespace Qt::StringLiterals;

class TestScroll : public QObject
{
    Q_OBJECT

    QQmlEngine m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_list = nullptr;  // scrolls vertically
    QQuickItem *m_strip = nullptr; // inside it, scrolls only sideways
    TouchpadScroll m_filter;
    QPointingDevice m_touchpad{u"touchpad"_s, 9001, QInputDevice::DeviceType::TouchPad,
                               QPointingDevice::PointerType::Finger,
                               QInputDevice::Capability::Position | QInputDevice::Capability::Scroll,
                               2, 0};
    QPointingDevice m_mouse{u"mouse"_s, 9002, QInputDevice::DeviceType::Mouse,
                            QPointingDevice::PointerType::Generic,
                            QInputDevice::Capability::Position | QInputDevice::Capability::Scroll,
                            1, 3};
    ulong m_clock = 1000;
    const QPointF m_open{100, 300}; // over the list, clear of the strip

    void wheel(Qt::ScrollPhase phase, QPoint pixels, QPoint angle, const QPointingDevice *device,
               Qt::MouseEventSource source, QPointF at)
    {
        QWheelEvent e(at, m_window->mapToGlobal(at), pixels, angle, Qt::NoButton, Qt::NoModifier,
                      phase, false, source, device);
        e.setTimestamp(m_clock);
        QCoreApplication::sendEvent(m_window.get(), &e);
    }

    /// As Qt's Wayland plugin reports a two-finger swipe: begin, `steps`
    /// pixel updates every 10 ms, then the end when the fingers lift,
    /// `rest` ms after they last moved.
    void waylandSwipe(int dy, int steps, int rest, QPointF at)
    {
        wheel(Qt::ScrollBegin, {}, {}, &m_mouse, Qt::MouseEventNotSynthesized, at);
        for (int i = 0; i < steps; ++i) {
            m_clock += 10;
            wheel(Qt::ScrollUpdate, {0, dy}, {0, dy * 8}, &m_mouse,
                  Qt::MouseEventSynthesizedBySystem, at);
        }
        m_clock += rest;
        wheel(Qt::ScrollEnd, {}, {}, &m_mouse, Qt::MouseEventNotSynthesized, at);
    }

    /// As Qt's X11 plugin reports one: bare wheel angles from a touchpad.
    void x11Swipe(int angle, int steps)
    {
        for (int i = 0; i < steps; ++i) {
            m_clock += 10;
            wheel(Qt::NoScrollPhase, {}, {0, angle}, &m_touchpad, Qt::MouseEventNotSynthesized,
                  m_open);
        }
    }

    qreal contentY() const { return m_list->property("contentY").toReal(); }
    bool moving() const { return m_list->property("moving").toBool(); }
    bool flicking() const { return m_list->property("flicking").toBool(); }

private slots:
    void initTestCase()
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQmlComponent component(&m_engine);
        component.setData(R"(
            import QtQuick
            Flickable {
                width: 400; height: 400
                contentHeight: 40000
                boundsBehavior: Flickable.StopAtBounds
                Flickable {
                    objectName: "strip"
                    width: 400; height: 200
                    contentWidth: 2000; contentHeight: 200
                    Rectangle { width: 2000; height: 200; color: "grey" }
                }
            }
        )", QUrl());
        m_list = qobject_cast<QQuickItem *>(component.create());
        QVERIFY2(m_list, qPrintable(component.errorString()));
        m_strip = m_list->findChild<QQuickItem *>(u"strip"_s);
        QVERIFY(m_strip);
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(400, 400);
        m_list->setParentItem(m_window->contentItem());
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        qApp->installEventFilter(&m_filter);
    }

    void init()
    {
        QTRY_VERIFY(!moving());
        m_list->setProperty("contentY", 0);
        m_strip->setProperty("contentX", 0);
        m_clock += 1000;
    }

    void waylandGlidesOnAfterAQuickSwipe()
    {
        const qreal deceleration = m_list->property("flickDeceleration").toReal();
        waylandSwipe(-30, 10, 8, m_open); // 3000 px/s
        QCOMPARE(contentY(), 300.0);      // followed the fingers
        QVERIFY(flicking());
        QTRY_VERIFY(!moving());
        // About 3000 px/s × GlideTime further, not a runaway.
        QVERIFY2(contentY() > 300 + 0.8 * 3000 * TouchpadScroll::GlideTime,
                 qPrintable(QString::number(contentY())));
        QVERIFY2(contentY() < 300 + 1.2 * 3000 * TouchpadScroll::GlideTime,
                 qPrintable(QString::number(contentY())));
        // Touch flicking keeps its own settings.
        QCOMPARE(m_list->property("flickDeceleration").toReal(), deceleration);
    }

    void fasterSwipesGoFurther()
    {
        waylandSwipe(-10, 10, 8, m_open); // 1000 px/s
        QTRY_VERIFY(!moving());
        const qreal slow = contentY() - 100;
        init();
        waylandSwipe(-40, 10, 8, m_open); // 4000 px/s
        QTRY_VERIFY(!moving());
        const qreal fast = contentY() - 400;
        QVERIFY2(fast > 3 * slow, qPrintable(u"%1 vs %2"_s.arg(fast).arg(slow)));
    }

    void noGlideWhenTheFingersRestedFirst()
    {
        waylandSwipe(-30, 10, 200, m_open);
        QVERIFY(!flicking());
        QTest::qWait(100);
        QCOMPARE(contentY(), 300.0);
    }

    void noGlideAfterSlowScrolling()
    {
        waylandSwipe(-1, 20, 8, m_open); // 100 px/s: reading line by line
        QVERIFY(!flicking());
        QTest::qWait(100);
        QCOMPARE(contentY(), 20.0);
    }

    void x11FollowsTheFingersThenGlides()
    {
        x11Swipe(-240, 10); // 30 px every 10 ms
        QCOMPARE(contentY(), 300.0);
        QVERIFY(!flicking());
        // The end comes once the touchpad goes quiet.
        QTRY_VERIFY(flicking());
        QTRY_VERIFY(!moving());
        QVERIFY2(contentY() > 300 + 0.8 * 3000 * TouchpadScroll::GlideTime,
                 qPrintable(QString::number(contentY())));
    }

    void x11CarriesFractionsOver()
    {
        x11Swipe(-4, 40); // half a pixel each
        QCOMPARE(contentY(), 20.0);
    }

    void aNewSwipeStopsAGlide()
    {
        waylandSwipe(-30, 10, 8, m_open);
        QVERIFY(flicking());
        QTest::qWait(50);
        m_clock += 50;
        wheel(Qt::ScrollBegin, {}, {}, &m_mouse, Qt::MouseEventNotSynthesized, m_open);
        QVERIFY(!flicking());
        const qreal stopped = contentY();
        m_clock += 300;
        wheel(Qt::ScrollEnd, {}, {}, &m_mouse, Qt::MouseEventNotSynthesized, m_open);
        QTest::qWait(100);
        // Settled to the whole pixel, and no further.
        QVERIFY2(qAbs(contentY() - stopped) < 1, qPrintable(u"%1 vs %2"_s.arg(contentY()).arg(stopped)));
    }

    void glidesWhatWasScrolledNotWhatIsUnderThePointer()
    {
        // Vertical swipe over the sideways strip: the list takes it.
        waylandSwipe(-30, 5, 8, QPointF(100, 100));
        QCOMPARE(contentY(), 150.0);
        QTRY_VERIFY(!moving());
        QVERIFY(contentY() > 400);
        QCOMPARE(m_strip->property("contentX").toReal(), 0.0);
    }

    void mouseWheelIsLeftAlone()
    {
        m_clock += 10;
        QWheelEvent e(m_open, m_window->mapToGlobal(m_open), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false,
                      Qt::MouseEventNotSynthesized, &m_mouse);
        e.setTimestamp(m_clock);
        QVERIFY(!m_filter.eventFilter(m_window.get(), &e));
        QCoreApplication::sendEvent(m_window.get(), &e);
        QTRY_VERIFY(!moving());
        // Qt's own wheel step: a few lines, no glide.
        QVERIFY2(contentY() > 0 && contentY() < 300, qPrintable(QString::number(contentY())));
    }
};

QTEST_MAIN(TestScroll)
#include "tst_scroll.moc"
