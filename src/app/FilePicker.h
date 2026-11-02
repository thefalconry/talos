// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QUrl>

class QFileDialog;
class QWindow;

namespace talos {

/// The system's own dialog for choosing files or a folder, for QML.
///
/// On Linux, desktop environments (like KDE Plasma and GNOME) provide their own
/// native file dialogs — such as KDE's KIO file dialog and the XDG Desktop
/// Portal FileChooser — offering bookmarks, network places, Dolphin/Nautilus
/// integration, and theme consistency.
///
/// FilePicker prefers the active desktop environment's native file chooser (via
/// kdialog for direct KIO integration on KDE, or the XDG Portal over D-Bus, or
/// zenity on GNOME), and falls back gracefully to QFileDialog if native tools
/// are unavailable.
class FilePicker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title MEMBER m_title)
    Q_PROPERTY(FileMode fileMode MEMBER m_mode)
    Q_PROPERTY(QStringList nameFilters MEMBER m_filters)
    Q_PROPERTY(QString defaultSuffix MEMBER m_suffix)
    /// For saving: the file first offered.
    Q_PROPERTY(QUrl currentFile MEMBER m_current)
    Q_PROPERTY(QUrl selectedFile READ selectedFile NOTIFY accepted)
    Q_PROPERTY(QList<QUrl> selectedFiles READ selectedFiles NOTIFY accepted)
    Q_PROPERTY(QUrl selectedFolder READ selectedFile NOTIFY accepted)
public:
    enum FileMode { OpenFile, OpenFiles, SaveFile, Folder };
    Q_ENUM(FileMode)

    explicit FilePicker(QObject *parent = nullptr);
    ~FilePicker() override;

    QUrl selectedFile() const { return m_selected.value(0); }
    QList<QUrl> selectedFiles() const { return m_selected; }

    Q_INVOKABLE void open();
    Q_INVOKABLE void close();

signals:
    void accepted();
    void rejected();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
#if defined(TALOS_HAVE_DBUS)
    void onPortalResponse(uint response, const QVariantMap &results);
#endif

private:
    bool openPortal(QWindow *owner);
    bool openKdialog(QWindow *owner);
    bool openZenity(QWindow *owner);
    void openQtDialog(QWindow *owner);

    void finishWithUrls(const QList<QUrl> &urls);
    void finishRejected();
    void cleanup();

    QString m_title;
    FileMode m_mode = OpenFile;
    QStringList m_filters;
    QString m_suffix;
    QUrl m_current;
    QList<QUrl> m_selected;

    bool m_active = false;
    QPointer<QFileDialog> m_dialog;
    QPointer<QProcess> m_process;
#if defined(TALOS_HAVE_DBUS)
    QString m_portalRequestPath;
#endif
};

} // namespace talos
