// SPDX-License-Identifier: GPL-2.0-only
#include "app/FilePicker.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QWindow>

namespace talos {

FilePicker::FilePicker(QObject *parent) : QObject(parent) {}

FilePicker::~FilePicker()
{
    delete m_dialog;
}

void FilePicker::open()
{
    if (m_dialog)
        return;
    // The window it belongs to, noted before the dialog exists and could
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
    if (!m_suffix.isEmpty())
        dialog->setDefaultSuffix(m_suffix);
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
        m_selected = dialog->selectedUrls();
        if (!m_selected.isEmpty())
            emit accepted();
    });
    connect(dialog, &QFileDialog::rejected, this, &FilePicker::rejected);
    dialog->open();
}

void FilePicker::close()
{
    if (m_dialog)
        m_dialog->reject();
}

} // namespace talos
