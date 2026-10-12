// SPDX-License-Identifier: GPL-2.0-only
#include "app/DesktopMac.h"

#include "app/Desktop.h"
#include "core/AppLog.h"

#include <QCoreApplication>
#include <QProcess>

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

using namespace Qt::StringLiterals;

namespace {
NSString *const kChatCategory = @"talos.chat";
NSString *const kReplyCategory = @"talos.chat.reply";
NSString *const kReplyAction = @"talos.reply";
NSString *const kChatKey = @"chat";
} // namespace

/// Receives what the person did with a notification, on a thread of the
/// system's, and passes it to Desktop on Qt's.
@interface TalosNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@property(atomic, assign) talos::Desktop *desktop;
@end

@implementation TalosNotificationDelegate

// The app only notifies about a chat that is not on screen, so a
// notification is wanted even while the app is in front.
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
didReceiveNotificationResponse:(UNNotificationResponse *)response
         withCompletionHandler:(void (^)(void))completionHandler
{
    NSString *chat = response.notification.request.content.userInfo[kChatKey];
    const QString chatId = QString::fromNSString(chat ?: @"");
    QString reply;
    if ([response isKindOfClass:[UNTextInputNotificationResponse class]])
        reply = QString::fromNSString(((UNTextInputNotificationResponse *)response).userText);
    const bool opened = [response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier];
    talos::Desktop *desktop = self.desktop;
    if (desktop) {
        QMetaObject::invokeMethod(desktop, [desktop, chatId, reply, opened] {
            if (!reply.trimmed().isEmpty() && !chatId.isEmpty())
                emit desktop->replyRequested(chatId, reply);
            else if (opened && !chatId.isEmpty())
                emit desktop->openChatRequested(chatId);
            else if (opened)
                emit desktop->activateRequested();
        }, Qt::QueuedConnection);
    }
    completionHandler();
}

@end

namespace talos::mac {

namespace {
TalosNotificationDelegate *g_delegate = nil;
/// Set once macOS has said this app may not use the Notification Centre at
/// all, which is its answer to an app without a developer's signature.
bool g_refused = false;

/// The way round for such a build: the system's script runner posts the
/// notification under its own name. It cannot be clicked through to a chat
/// or answered, but the person is told.
void postThroughScript(const QString &title, const QString &body)
{
    // Title and body travel as arguments, never as script text.
    QProcess::startDetached(u"/usr/bin/osascript"_s,
                            {u"-e"_s, u"on run argv"_s,
                             u"-e"_s, u"display notification (item 2 of argv) with title (item 1 of argv)"_s,
                             u"-e"_s, u"end run"_s, u"--"_s, title, body});
}
} // namespace

bool notificationsAvailable()
{
    // Asking for the centre without a bundle identifier is a crash, and a
    // program run from outside its bundle (the tests, a build tree's bare
    // binary) has none.
    return NSBundle.mainBundle.bundleIdentifier.length > 0
        && [NSBundle.mainBundle.bundlePath.pathExtension isEqualToString:@"app"];
}

void setupNotifications(Desktop *desktop)
{
    if (!notificationsAvailable() || g_delegate)
        return;
    g_delegate = [[TalosNotificationDelegate alloc] init];
    g_delegate.desktop = desktop;
    UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
    center.delegate = g_delegate;
    UNTextInputNotificationAction *reply =
        [UNTextInputNotificationAction actionWithIdentifier:kReplyAction
                                                      title:@"Reply"
                                                    options:UNNotificationActionOptionNone
                                       textInputButtonTitle:@"Send"
                                       textInputPlaceholder:@"Reply to Talon"];
    UNNotificationCategory *chat =
        [UNNotificationCategory categoryWithIdentifier:kChatCategory
                                               actions:@[]
                                     intentIdentifiers:@[]
                                               options:UNNotificationCategoryOptionNone];
    UNNotificationCategory *answerable =
        [UNNotificationCategory categoryWithIdentifier:kReplyCategory
                                               actions:@[reply]
                                     intentIdentifiers:@[]
                                               options:UNNotificationCategoryOptionNone];
    [center setNotificationCategories:[NSSet setWithObjects:chat, answerable, nil]];
}

void forgetNotifications(Desktop *desktop)
{
    // The delegate outlives the app's objects: the system may still call it
    // while the app is on its way out.
    if (g_delegate && g_delegate.desktop == desktop)
        g_delegate.desktop = nullptr;
}

bool postNotification(const QString &chatId, const QString &title, const QString &body,
                      bool canReply)
{
    if (!notificationsAvailable())
        return false;
    if (g_refused) {
        postThroughScript(title, body);
        return true;
    }
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = title.toNSString();
    content.body = body.toNSString();
    content.sound = UNNotificationSound.defaultSound;
    if (!chatId.isEmpty()) {
        content.userInfo = @{kChatKey: chatId.toNSString()};
        content.categoryIdentifier = canReply ? kReplyCategory : kChatCategory;
        // Later replies in one chat stack under the first.
        content.threadIdentifier = chatId.toNSString();
    }
    UNNotificationRequest *request =
        [UNNotificationRequest requestWithIdentifier:NSUUID.UUID.UUIDString
                                             content:content
                                             trigger:nil];
    UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
    // The answer comes later, on a thread of the system's, when the caller's
    // strings are long gone. A block takes a C++ reference as a reference,
    // not as a copy, so what it needs is copied here, by name.
    const QString titleCopy = title;
    const QString bodyCopy = body;
    // Asks the person the first time; afterwards it only reports the answer.
    [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
                          completionHandler:^(BOOL granted, NSError *error) {
        if (granted) {
            [center addNotificationRequest:request withCompletionHandler:nil];
            return;
        }
        // No error: the person said no, here or in System Settings, and
        // that is respected. An error: macOS will not deal with this build
        // at all (it is not signed by a developer), which is nobody's
        // choice, so the notification goes the other way.
        const bool refused = error != nil;
        QMetaObject::invokeMethod(qApp, [refused, title = titleCopy, body = bodyCopy] {
            if (!refused) {
                static bool said = false;
                if (!said) {
                    said = true;
                    AppLog::info("desktop", u"notifications are switched off for Talos in System "
                                            "Settings › Notifications"_s);
                }
                return;
            }
            if (!g_refused) {
                g_refused = true;
                AppLog::warn("desktop", u"macOS only lets an app signed by a developer use the "
                                        "Notification Centre; notifications are posted through "
                                        "the system's script runner instead, without replies"_s);
            }
            postThroughScript(title, body);
        }, Qt::QueuedConnection);
    }];
    return true;
}

} // namespace talos::mac
