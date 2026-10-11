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

    /// True when this desktop offers remote control through its portal.
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
    QJsonObject baseReply(const QSize &space) const;
    void notify(const QString &method, const QVariantList &args);

    Store *m_store;
    bool m_ready = false;
    bool m_starting = false;
    bool m_busy = false;
    QString m_session;
    uint m_stream = 0;
    QSize m_streamSize;
    QPoint m_streamPos;
    QPointF m_cursor{-1, -1}; ///< in stream coordinates; -1 until we moved it
    QList<std::function<void(const QString &)>> m_waiters;
    int m_tokens = 0;
};

} // namespace talos
