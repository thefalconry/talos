// SPDX-License-Identifier: GPL-2.0-only
#include "app/FilePicker.h"

#include "mesh/Platform.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QWindow>

#if defined(TALOS_HAVE_DBUS)
#  include <QDBusArgument>
#  include <QDBusConnection>
#  include <QDBusInterface>
#  include <QDBusMessage>
#  include <QDBusObjectPath>
#  include <QDBusPendingCallWatcher>
#  include <QDBusPendingReply>
#  include <QDBusVariant>
#endif

namespace talos {

using namespace Qt::StringLiterals;

FilePicker::FilePicker(QObject *parent) : QObject(parent) {}

FilePicker::~FilePicker()
{
    close();
}

void FilePicker::open()
{
    if (m_active)
        return;
    m_active = true;
    m_selected.clear();

    // The window it belongs to, noted before any dialog exists and could
    // be taken for it.
    QWindow *owner = QGuiApplication::focusWindow();
    if (!owner) {
        const auto windows = QGuiApplication::topLevelWindows();
        for (QWindow *window : windows) {
            if (window->isVisible()) {
                owner = window;
                break;
            }
        }
    }

#if defined(Q_OS_LINUX)
    const QString currentDesktop = QString::fromLatin1(qgetenv("XDG_CURRENT_DESKTOP")).toLower();
    const bool isKde = currentDesktop.contains("kde"_L1) || currentDesktop.contains("plasma"_L1);
    const bool inSandbox = platform::isSandboxed();

    // 1. Inside a sandbox (Flatpak/Snap), always talk to the host XDG portal first.
    if (inSandbox) {
        if (openPortal(owner))
            return;
    }

    // 2. On KDE Plasma, kdialog provides direct access to KDE's native KIO file dialog.
    if (isKde) {
        if (openKdialog(owner))
            return;
        if (openPortal(owner))
            return;
    } else {
        // 3. On GNOME and other desktop environments, try XDG Portal then zenity.
        if (openPortal(owner))
            return;
        if (openZenity(owner))
            return;
    }
#endif

    openQtDialog(owner);
}

void FilePicker::close()
{
#if defined(TALOS_HAVE_DBUS)
    if (!m_portalRequestPath.isEmpty()) {
        QDBusMessage closeMsg = QDBusMessage::createMethodCall(
            u"org.freedesktop.portal.Desktop"_s,
            m_portalRequestPath,
            u"org.freedesktop.portal.Request"_s,
            u"Close"_s);
        QDBusConnection::sessionBus().send(closeMsg);
    }
#endif
    if (m_process) {
        m_process->kill();
    }
    if (m_dialog) {
        m_dialog->reject();
    }
    cleanup();
}

void FilePicker::finishWithUrls(const QList<QUrl> &urls)
{
    m_selected = urls;
    m_active = false;
    if (!m_selected.isEmpty())
        emit accepted();
    else
        emit rejected();
}

void FilePicker::finishRejected()
{
    m_active = false;
    emit rejected();
}

void FilePicker::cleanup()
{
    m_active = false;
#if defined(TALOS_HAVE_DBUS)
    if (!m_portalRequestPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            u"org.freedesktop.portal.Desktop"_s,
            m_portalRequestPath,
            u"org.freedesktop.portal.Request"_s,
            u"Response"_s,
            this,
            SLOT(onPortalResponse(uint, QVariantMap)));
        m_portalRequestPath.clear();
    }
#endif
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_dialog) {
        m_dialog->deleteLater();
        m_dialog = nullptr;
    }
}

void FilePicker::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_process) {
        cleanup();
        return;
    }
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        const QString output = QString::fromUtf8(m_process->readAllStandardOutput()).trimmed();
        cleanup();
        if (output.isEmpty()) {
            finishRejected();
            return;
        }
        QList<QUrl> urls;
        const QStringList lines = output.split(u'\n', Qt::SkipEmptyParts);
        for (const QString &raw : lines) {
            QString path = raw.trimmed();
            if (!path.isEmpty()) {
                if (m_mode == SaveFile && !m_suffix.isEmpty() && !path.contains(u'.'))
                    path += u'.' + m_suffix;
                urls.append(QUrl::fromLocalFile(path));
            }
        }
        finishWithUrls(urls);
    } else {
        cleanup();
        finishRejected();
    }
}

#if defined(Q_OS_LINUX)

bool FilePicker::openKdialog(QWindow *owner)
{
    const QString kdialogExe = QStandardPaths::findExecutable(u"kdialog"_s);
    if (kdialogExe.isEmpty())
        return false;

    QString startDir;
    QString startPath;
    if (m_current.isLocalFile()) {
        const QFileInfo offered(m_current.toLocalFile());
        startDir = offered.isDir() ? offered.absoluteFilePath() : offered.absolutePath();
        startPath = offered.absoluteFilePath();
    }
    if (startDir.isEmpty())
        startDir = QDir::homePath();
    if (startPath.isEmpty())
        startPath = startDir;

    // Build KDE filter string: "*.md *.txt | Markdown files\n* | All files"
    QString filterArg;
    if (!m_filters.isEmpty()) {
        QStringList kdeFilterList;
        for (const QString &f : m_filters) {
            const int startParen = f.indexOf(u'(');
            const int endParen = f.indexOf(u')', startParen);
            if (startParen != -1 && endParen > startParen) {
                const QString label = f.left(startParen).trimmed();
                const QString globs = f.mid(startParen + 1, endParen - startParen - 1).trimmed();
                kdeFilterList.append(globs + u" | "_s + label);
            } else {
                kdeFilterList.append(f);
            }
        }
        filterArg = kdeFilterList.join(u'\n');
    }

    QStringList args;
    if (!m_title.isEmpty())
        args << u"--title"_s << m_title;
    if (owner && owner->winId())
        args << u"--attach"_s << QString::number(owner->winId());

    switch (m_mode) {
    case OpenFile:
        args << u"--getopenfilename"_s << startDir;
        if (!filterArg.isEmpty())
            args << filterArg;
        break;
    case OpenFiles:
        args << u"--getopenfilename"_s << startDir;
        if (!filterArg.isEmpty())
            args << filterArg;
        args << u"--multiple"_s << u"--separate-output"_s;
        break;
    case SaveFile:
        args << u"--getsavefilename"_s << startPath;
        if (!filterArg.isEmpty())
            args << filterArg;
        break;
    case Folder:
        args << u"--getexistingdirectory"_s << startDir;
        break;
    }

    auto *proc = new QProcess(this);
    m_process = proc;
    connect(proc, &QProcess::finished, this, &FilePicker::onProcessFinished);
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        cleanup();
        emit rejected();
    });
    proc->start(kdialogExe, args);
    return proc->waitForStarted(250);
}

bool FilePicker::openZenity(QWindow *owner)
{
    const QString zenityExe = QStandardPaths::findExecutable(u"zenity"_s);
    if (zenityExe.isEmpty())
        return false;

    QString startPath;
    if (m_current.isLocalFile()) {
        startPath = m_current.toLocalFile();
    }
    if (startPath.isEmpty())
        startPath = QDir::homePath();

    QStringList args;
    args << u"--file-selection"_s;
    if (!m_title.isEmpty())
        args << u"--title="_s + m_title;
    if (!startPath.isEmpty())
        args << u"--filename="_s + startPath;

    switch (m_mode) {
    case OpenFile:
        break;
    case OpenFiles:
        args << u"--multiple"_s << u"--separator=\n"_s;
        break;
    case SaveFile:
        args << u"--save"_s << u"--confirm-overwrite"_s;
        break;
    case Folder:
        args << u"--directory"_s;
        break;
    }

    for (const QString &f : m_filters) {
        const int startParen = f.indexOf(u'(');
        const int endParen = f.indexOf(u')', startParen);
        if (startParen != -1 && endParen > startParen) {
            const QString label = f.left(startParen).trimmed();
            const QString globs = f.mid(startParen + 1, endParen - startParen - 1).trimmed();
            args << u"--file-filter="_s + label + u" | "_s + globs;
        } else {
            args << u"--file-filter="_s + f;
        }
    }

    auto *proc = new QProcess(this);
    m_process = proc;
    connect(proc, &QProcess::finished, this, &FilePicker::onProcessFinished);
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        cleanup();
        emit rejected();
    });
    proc->start(zenityExe, args);
    return proc->waitForStarted(250);
}

bool FilePicker::openPortal(QWindow *owner)
{
#  if defined(TALOS_HAVE_DBUS)
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusInterface portal(u"org.freedesktop.portal.Desktop"_s,
                          u"/org/freedesktop/portal/desktop"_s,
                          u"org.freedesktop.portal.FileChooser"_s,
                          bus);
    if (!portal.isValid())
        return false;

    QString parentHandle;
    if (owner && owner->winId()) {
        const QString platformName = QGuiApplication::platformName();
        if (platformName.contains("xcb"_L1) || platformName.contains("x11"_L1))
            parentHandle = u"x11:"_s + QString::number(owner->winId(), 16);
    }

    const QString token = u"talos_picker_%1"_s.arg(QRandomGenerator::global()->generate());
    QVariantMap options;
    options.insert(u"handle_token"_s, token);

    if (m_mode == OpenFiles)
        options.insert(u"multiple"_s, true);
    else if (m_mode == Folder)
        options.insert(u"directory"_s, true);

    if (m_current.isLocalFile()) {
        const QFileInfo offered(m_current.toLocalFile());
        if (offered.isDir()) {
            options.insert(u"current_folder"_s, offered.absoluteFilePath().toUtf8() + '\0');
        } else {
            options.insert(u"current_folder"_s, offered.absolutePath().toUtf8() + '\0');
            if (m_mode == SaveFile)
                options.insert(u"current_name"_s, offered.fileName());
        }
    }

    const QString method = (m_mode == SaveFile) ? u"SaveFile"_s : u"OpenFile"_s;
    const QString dialogTitle = m_title.isEmpty()
        ? (m_mode == SaveFile ? tr("Save File") : tr("Open File")) : m_title;

    QDBusMessage call = QDBusMessage::createMethodCall(
        u"org.freedesktop.portal.Desktop"_s,
        u"/org/freedesktop/portal/desktop"_s,
        u"org.freedesktop.portal.FileChooser"_s,
        method);
    call.setArguments({parentHandle, dialogTitle, options});

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            cleanup();
            finishRejected();
            return;
        }
        m_portalRequestPath = reply.value().path();
        QDBusConnection::sessionBus().connect(
            u"org.freedesktop.portal.Desktop"_s,
            m_portalRequestPath,
            u"org.freedesktop.portal.Request"_s,
            u"Response"_s,
            this,
            SLOT(onPortalResponse(uint, QVariantMap)));
    });
    return true;
#  else
    Q_UNUSED(owner);
    return false;
#  endif
}

#  if defined(TALOS_HAVE_DBUS)
void FilePicker::onPortalResponse(uint response, const QVariantMap &results)
{
    cleanup();
    if (response == 0) {
        const QStringList uris = results.value(u"uris"_s).toStringList();
        QList<QUrl> urls;
        for (const QString &uri : uris) {
            QUrl url(uri);
            if (m_mode == SaveFile && !m_suffix.isEmpty() && url.isLocalFile()) {
                QString local = url.toLocalFile();
                if (!local.contains(u'.')) {
                    local += u'.' + m_suffix;
                    url = QUrl::fromLocalFile(local);
                }
            }
            urls.append(url);
        }
        finishWithUrls(urls);
    } else {
        finishRejected();
    }
}
#  endif

#endif // Q_OS_LINUX

void FilePicker::openQtDialog(QWindow *owner)
{
    auto *dialog = new QFileDialog;
    m_dialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(m_title);
    switch (m_mode) {
    case OpenFile:
        dialog->setFileMode(QFileDialog::ExistingFile);
        break;
    case OpenFiles:
        dialog->setFileMode(QFileDialog::ExistingFiles);
        break;
    case SaveFile:
        dialog->setFileMode(QFileDialog::AnyFile);
        dialog->setAcceptMode(QFileDialog::AcceptSave);
        break;
    case Folder:
        dialog->setFileMode(QFileDialog::Directory);
        dialog->setOption(QFileDialog::ShowDirsOnly);
        break;
    }
    if (!m_filters.isEmpty())
        dialog->setNameFilters(m_filters);
#if !defined(Q_OS_ANDROID)
    // Android's chooser answers with a content:// URI that names a document
    // by number, not by file name. A suffix added to that is the address of
    // nothing, and the save fails after the (empty) file was made.
    if (!m_suffix.isEmpty())
        dialog->setDefaultSuffix(m_suffix);
#endif
    if (m_current.isLocalFile()) {
        const QFileInfo offered(m_current.toLocalFile());
        dialog->setDirectory(offered.absolutePath());
        dialog->selectFile(offered.fileName());
    }
#if !defined(Q_OS_ANDROID)
    // A sheet on the window it was asked from, where the system has those,
    // and in any case a dialog of that window rather than of nothing. The
    // main window is not a widget, so the tie is made between the windows.
    dialog->setWindowModality(Qt::WindowModal);
    if (owner) {
        dialog->winId();
        if (QWindow *own = dialog->windowHandle())
            own->setTransientParent(owner);
    }
#else
    Q_UNUSED(owner);
#endif
    connect(dialog, &QFileDialog::accepted, this, [this, dialog] {
        const auto urls = dialog->selectedUrls();
        cleanup();
        finishWithUrls(urls);
    });
    connect(dialog, &QFileDialog::rejected, this, [this] {
        cleanup();
        finishRejected();
    });
    dialog->open();
}

} // namespace talos
