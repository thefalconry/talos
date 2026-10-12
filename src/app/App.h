// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "app/AppLock.h"
#include "app/Desktop.h"
#include "mesh/ScreenControl.h"
#include "app/Updater.h"
#include "app/Voice.h"
#include "core/AppLog.h"
#include "core/BridgeLinks.h"
#include "core/MediaStore.h"
#include "core/Session.h"
#include "core/Store.h"
#include "mesh/MeshService.h"

#include <QColor>
#include <QJSValue>
#include <QObject>
#include <QUrl>

class QQmlEngine;
class QQuickWindow;

namespace talos {

/// The one object QML talks to. It owns every service and exposes them as
/// properties (`App.session`, `App.prefs`, `App.mesh`, …), plus the small
/// amount of glue that belongs to no single service: building a connection
/// profile from the connect form, the clipboard, notifications, and the
/// approval prompt for mesh commands.
class App : public QObject
{
    Q_OBJECT
    Q_PROPERTY(talos::Session *session READ session CONSTANT)
    Q_PROPERTY(talos::Store *store READ store CONSTANT)
    Q_PROPERTY(QQmlPropertyMap *prefs READ prefs CONSTANT)
    Q_PROPERTY(talos::MeshService *mesh READ mesh CONSTANT)
    Q_PROPERTY(talos::MediaStore *media READ media CONSTANT)
    Q_PROPERTY(talos::BridgeLinks *links READ links CONSTANT)
    Q_PROPERTY(talos::AppLock *lock READ lock CONSTANT)
    Q_PROPERTY(talos::Updater *updater READ updater CONSTANT)
    Q_PROPERTY(talos::Voice *voice READ voice CONSTANT)
    Q_PROPERTY(talos::Desktop *desktop READ desktop CONSTANT)
    Q_PROPERTY(talos::ScreenControl *screen READ screen CONSTANT)
    Q_PROPERTY(bool serviceSupported READ serviceSupported CONSTANT)
    Q_PROPERTY(QString serviceState READ serviceState NOTIFY serviceChanged)
    Q_PROPERTY(bool serviceLingering READ serviceLingering NOTIFY serviceChanged)
    Q_PROPERTY(talos::AppLog *log READ log CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(bool mobile READ mobile CONSTANT)
    Q_PROPERTY(bool systemDark READ systemDark NOTIFY systemDarkChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantList approvals READ approvals NOTIFY approvalsChanged)
public:
    explicit App(QObject *parent = nullptr);
    ~App() override;

    void attach(QQmlEngine *engine, QQuickWindow *window);
    /// A `talon://pair` link or bridge URL handed to the app from outside.
    Q_INVOKABLE void handleExternalLink(const QString &link);
    /// The system opened a `talon:` URL with this app (QDesktopServices).
    Q_INVOKABLE void handleExternalUrl(const QUrl &url) { handleExternalLink(url.toString()); }

    Session *session() { return &m_session; }
    Store *store() { return &m_store; }
    QQmlPropertyMap *prefs() { return m_store.prefs(); }
    MeshService *mesh() { return &m_mesh; }
    MediaStore *media() { return &m_media; }
    BridgeLinks *links() { return &m_links; }
    AppLock *lock() { return &m_lock; }
    Updater *updater() { return &m_updater; }
    Voice *voice() { return &m_voice; }
    Desktop *desktop() { return &m_desktop; }
    ScreenControl *screen() { return &m_screen; }
    bool serviceSupported() const;
    QString serviceState() const;
    bool serviceLingering() const;
    /// Install or remove the background service that keeps this device on
    /// the mesh while the app is closed. Returns an error, or empty.
    Q_INVOKABLE QString setBackgroundService(bool on);
    Q_INVOKABLE void refreshService() { emit serviceChanged(); }
    AppLog *log() { return AppLog::instance(); }
    QString version() const { return QStringLiteral(TALOS_VERSION); }
    QString platform() const;
    bool mobile() const;
    bool systemDark() const;
    bool active() const { return m_active; }
    QVariantList approvals() const { return m_approvals; }

    // ── Connection profiles ─────────────────────────────────────────────────
    /// A blank form for a new profile.
    Q_INVOKABLE QVariantMap newProfileForm() const;
    /// The form for an existing profile. Credentials are never handed to
    /// QML; the form only learns whether they are set.
    Q_INVOKABLE QVariantMap profileForm(const QString &id) const;
    /// The form a pairing link fills in, or `{error}`.
    Q_INVOKABLE QVariantMap formFromPairLink(const QString &text) const;
    /// True for the address of a daemon's pairing page (`…/pair?grant=…`),
    /// which redeemPairPage can trade for the connection it stands for.
    Q_INVOKABLE bool isPairPage(const QString &text) const;
    /// Fetch a pairing page's payload and answer like formFromPairLink.
    /// The grant in the address is spent by the attempt.
    Q_INVOKABLE void redeemPairPage(const QString &address, QJSValue done);
    /// Validate a form. Returns `{ok, error, needsFingerprint}`.
    Q_INVOKABLE QVariantMap checkForm(const QVariantMap &form) const;
    /// Look at the certificate the form's server presents, sending no
    /// credential. The callback gets `{reached, fingerprint, pretty}`.
    Q_INVOKABLE void probeForm(const QVariantMap &form, QJSValue done);
    /// Save the form as the active profile and connect.
    Q_INVOKABLE void connectWithForm(const QVariantMap &form);
    /// Read a .p12/.pfx and check it opens. Returns `{ok, error, p12, name}`.
    Q_INVOKABLE QVariantMap importCertificate(const QUrl &file, const QString &password) const;
    Q_INVOKABLE void forgetEverything();

    // ── Small utilities for the views ───────────────────────────────────────
    /// Links inside rich text take their colour from the application
    /// palette, so the accent is pushed there.
    Q_INVOKABLE void setLinkColor(const QColor &color);
    Q_INVOKABLE void copy(const QString &text);
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE bool clipboardHasImage() const;
    Q_INVOKABLE bool saveText(const QUrl &file, const QString &text);
    Q_INVOKABLE QString timeLabel(qint64 ms) const;
    Q_INVOKABLE QString relativeTime(qint64 ms) const;
    Q_INVOKABLE QString dateTimeLabel(qint64 ms) const;
    Q_INVOKABLE QString fileName(const QUrl &file) const;
    Q_INVOKABLE QString mapLink(double lat, double lon) const;
    Q_INVOKABLE void resolveApproval(int id, bool allow);
    Q_INVOKABLE void showWindow();
    /// Leave the app running behind whatever was open before it (a phone's
    /// "back" at the top level). False where the platform has no such thing.
    Q_INVOKABLE bool moveToBackground();
    /// A starting file name for a save dialog, in the documents folder.
    Q_INVOKABLE QUrl suggestedFile(const QString &title, const QString &suffix) const;
    /// A file dialog's URL as a path on this device.
    Q_INVOKABLE QString localPath(const QUrl &url) const;
    Q_INVOKABLE void quit();
    /// Render the window to a PNG (development and documentation).
    Q_INVOKABLE bool grab(const QString &path);

signals:
    void systemDarkChanged();
    void activeChanged();
    void approvalsChanged();
    void serviceChanged();
    /// A pairing link arrived from outside the app.
    void pairLinkReceived(const QString &link);
    void toast(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Profile profileFromForm(const QVariantMap &form, QString *error) const;
    void onMessage(const QString &chatId, const QString &title, const QString &text,
                   bool fromAssistant);
    void updateTray();
    void updateKeepAlive();
    /// Set while the lock is being removed, so credentials are written back
    /// unsealed although the lock still exists for a moment.
    bool m_unsealing = false;

    Store m_store;
    Session m_session;
    MeshService m_mesh;
    MediaStore m_media;
    BridgeLinks m_links;
    AppLock m_lock;
    Updater m_updater;
    Voice m_voice;
    Desktop m_desktop;
    ScreenControl m_screen;

    QQmlEngine *m_engine = nullptr;
    QQuickWindow *m_window = nullptr;
    bool m_active = true;
    bool m_quitting = false;

    struct Approval {
        int id;
        std::function<void(const QString &)> decide;
    };
    QList<Approval> m_pending;
    QVariantList m_approvals;
    int m_approvalSeq = 0;
};

} // namespace talos
