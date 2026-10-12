// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

class QFileDialog;

namespace talos {

/// The system's own dialog for choosing files or a folder, for QML.
///
/// It stands where Qt Quick Dialogs' FileDialog and FolderDialog stood and
/// answers to the same names. Those bring a second, drawn implementation of
/// every dialog with them, and through it every Qt Quick Controls style:
/// some sixteen megabytes of libraries in an Android package, all loaded at
/// each start, for a fallback this app never shows. QFileDialog asks the
/// platform for the same native dialog and needs none of it.
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

private:
    QString m_title;
    FileMode m_mode = OpenFile;
    QStringList m_filters;
    QString m_suffix;
    QUrl m_current;
    QList<QUrl> m_selected;
    QPointer<QFileDialog> m_dialog;
};

} // namespace talos
