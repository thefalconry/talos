// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>

namespace talos {
class Desktop;
}

/// Notification Centre, which only speaks Objective-C. Used by Desktop.
namespace talos::mac {

/// True when this process is an app the Notification Centre will deal with:
/// one started from its bundle, not a bare program.
bool notificationsAvailable();

/// Tell the Notification Centre who answers when a notification is clicked
/// or replied to: @p desktop's openChatRequested and replyRequested.
void setupNotifications(Desktop *desktop);

/// Post a notification. With a @p chatId, clicking it opens that chat and,
/// when @p canReply, an answer can be typed into it. The first one makes
/// macOS ask whether Talos may notify at all. False when unavailable.
bool postNotification(const QString &chatId, const QString &title, const QString &body,
                      bool canReply);

} // namespace talos::mac
