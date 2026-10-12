// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "mesh/DeviceExec.h"

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QVariantMap>
#include <functional>

namespace talos {

class Store;

/// Lets Talon look at and operate this desktop: the mesh's `computer`
/// command (screenshot, move, click, drag, scroll, type, key).
///
/// On Linux this goes through the desktop portal: the compositor itself
/// asks the person whether to allow it, shows that a session is running,
/// and can end it. Nothing is injected behind the desktop's back. The
/// first use raises that consent dialog; the grant is remembered until it
/// is revoked here or in the system settings.
///
/// On macOS the pointer and keyboard are CoreGraphics events and `snapshot`
/// reads the accessibility tree, both of which the system only allows an
/// app the person has ticked under Privacy & Security › Accessibility;
/// screenshots need Screen Recording in the same place. The first use of
/// each raises the system's own request, and only the person can grant or
/// withdraw it.
///
/// Coordinates follow talon-node's contract: callers work in a "space"
/// whose longest edge is at most `maxEdge` (default 1280), and a
/// screenshot is rendered at that size, so a pixel in the picture and a
/// click target are the same pair of numbers.
class ScreenControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool granted READ granted NOTIFY changed)
public:
    explicit ScreenControl(Store *store, QObject *parent = nullptr);
    ~ScreenControl() override;

    /// True when this desktop offers remote control: through its portal on
    /// Linux, always on a Mac with a display.
    bool available() const;
    bool granted() const { return m_ready; }

    void handle(const QJsonObject &params, DeviceExec::Done done);
    /// End the session and forget the remembered grant.
    Q_INVOKABLE void revoke();

    // Pure parts, exposed for tests.
    static QSize spaceFor(const QSize &screen, int maxEdge, double *factor);
    /// X11 keysym for a key name ("escape", "f5", "a"), or 0.
    static int keysymFor(const QString &name);
    /// Split "ctrl+shift+s" into modifier keysyms and the key's keysym.
    static bool parseCombo(const QString &combo, QList<int> *modifiers, int *key, QString *error);
    /// JPEG that fits @p maxBytes, shrinking quality first, then size.
    static QByteArray fitJpeg(const QImage &image, int quality, int maxBytes, QSize *size,
                              int *usedQuality, QString *error);
    /// The Mac virtual key code for a named key's or modifier's keysym
    /// (Escape, F5, Shift…), or -1 for one that types a character.
    static int macKeyCode(int keysym);

signals:
    void changed();

private slots:
    void onSessionClosed();

private:
    struct Step {
        std::function<void()> run;
        int thenWaitMs = 0;
    };
    void ensureSession(std::function<void(const QString &error)> then);
    void perform(const QJsonObject &params, DeviceExec::Done done);
    void runSteps(QList<Step> steps, std::function<void()> then);
    void screenshot(const QJsonObject &params, DeviceExec::Done done);
    /// Scale a capture of the shared screen to the space and answer with it.
    void replyWithShot(QImage shot, const QJsonObject &params, DeviceExec::Done done);
    /// Whether this platform can read the accessibility tree at all.
    static bool canSnapshot();
    void snapshot(const QJsonObject &params, DeviceExec::Done done);
    /// Looks at what is under @p point (stream coordinates) now, and returns
    /// what to call once a click there has settled: it reports the control
    /// that was hit, how it changed, and what is under the pointer since.
    /// Null where the platform has no way to look.
    std::function<void(QJsonObject *)> clickProbe(const QPointF &point, double factor);
    QJsonObject baseReply(const QSize &space) const;
    void notify(const QString &method, const QVariantList &args);

    Store *m_store;
    bool m_ready = false;
    bool m_starting = false;
    bool m_busy = false;
    QString m_action; ///< the action being handled; which permission it needs differs
    QString m_session;
    uint m_stream = 0;
    QSize m_streamSize;
    QPoint m_streamPos;
    QPointF m_cursor{-1, -1}; ///< in stream coordinates; -1 until we moved it
    QList<std::function<void(const QString &)>> m_waiters;
    int m_tokens = 0;
};

} // namespace talos
