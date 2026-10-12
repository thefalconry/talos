// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QQmlNetworkAccessManagerFactory>

namespace talos {

/// Gives the QML engine a network access manager that refuses everything
/// that isn't local.
///
/// Text shown in the window comes from the model and from people the model
/// has read. Qt's text items fetch the images such text refers to, so a
/// reply containing `![](https://example.org/?q=…)` would otherwise make
/// the app call out to a server of the writer's choosing, with whatever the
/// writer put in the address. Nothing in the interface needs the engine to
/// reach the network: media comes through MediaStore over the bridge
/// connection, and links open only when clicked.
class NoNetworkFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override;
};

} // namespace talos
