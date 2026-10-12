// SPDX-License-Identifier: GPL-2.0-only
//
// Biometric on macOS: Touch ID in front of the login Keychain.
//
// The system confirms the finger and the key is then read from the
// Keychain, which is what protects it at rest. Having the Keychain itself
// demand the fingerprint would be stronger, but that kind of item is only
// open to apps signed with a developer identity; this is the same
// convenience unlock as fprintd on Linux, and the settings say so.
#include "app/Biometric.h"

#include "core/SecretStore.h"

#import <LocalAuthentication/LocalAuthentication.h>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

const QString kBioKey = u"applock-bio"_s;

/// The check in progress, kept so it can be called off.
LAContext *g_context = nil;

/// Whether Touch ID can be used right now, and the system's reason if not.
bool usable(NSError **why)
{
    LAContext *context = [[LAContext alloc] init];
    return [context canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:why];
}

QString explain(NSError *error)
{
    switch (error.code) {
    case LAErrorBiometryNotAvailable:
        return u"This Mac has no Touch ID, or it is switched off."_s;
    case LAErrorBiometryNotEnrolled:
        return u"No fingerprint is set up. Add one in System Settings › Touch ID & Password."_s;
    case LAErrorBiometryLockout:
        return u"Touch ID is locked after too many tries. Unlock the Mac with its password "
               "first."_s;
    case LAErrorUserCancel:
    case LAErrorAppCancel:
    case LAErrorSystemCancel:
        return u"Cancelled"_s;
    case LAErrorAuthenticationFailed:
        return u"That fingerprint wasn't recognised."_s;
    default:
        return QString::fromNSString(error.localizedDescription ?: @"Touch ID failed.");
    }
}

/// Ask for a finger, and answer on Qt's thread: the system calls back on
/// one of its own.
void verify(Biometric *self, const QString &reason, std::function<void(const QString &error)> done)
{
    LAContext *context = [[LAContext alloc] init];
    // The passcode is this app's own fallback, not the Mac's password.
    context.localizedFallbackTitle = @"";
    g_context = context;
    [context evaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
            localizedReason:reason.toNSString()
                      reply:^(BOOL success, NSError *error) {
        const QString why = success ? QString() : explain(error);
        QMetaObject::invokeMethod(self, [context, why, done] {
            if (g_context == context)
                g_context = nil;
            done(why);
        }, Qt::QueuedConnection);
    }];
}

} // namespace

bool Biometric::available() const
{
    // The key has to rest somewhere a fingerprint is not the only guard.
    return !SecretStore::instance().fileOnly() && usable(nil);
}

QString Biometric::unavailableReason() const
{
    if (SecretStore::instance().fileOnly())
        return u"Needs the Keychain. Credentials are kept in a private file here (as they "
               "are while the background service is installed), which would leave the key "
               "with no guard but the fingerprint check."_s;
    NSError *why = nil;
    return usable(&why) ? QString() : explain(why);
}

QString Biometric::name() const
{
    return u"Touch ID"_s;
}

bool Biometric::enrolled() const
{
    bool found = false;
    if (!SecretStore::instance().fileOnly())
        SecretStore::instance().read(kBioKey, &found);
    return found;
}

void Biometric::seal(const QByteArray &key, SealDone done)
{
    if (!available()) {
        done(false, unavailableReason());
        return;
    }
    // Prove the finger works before relying on it.
    verify(this, u"turn on Touch ID for Talos"_s, [key, done](const QString &error) {
        if (!error.isEmpty()) {
            done(false, error);
            return;
        }
        done(SecretStore::instance().write(kBioKey, key),
             u"Couldn't store the key in the Keychain."_s);
    });
}

void Biometric::open(OpenDone done)
{
    if (g_context) {
        done({}, u"A Touch ID check is already running."_s);
        return;
    }
    if (!available()) {
        done({}, unavailableReason());
        return;
    }
    verify(this, u"unlock Talos"_s, [done](const QString &error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }
        bool found = false;
        const QByteArray key = SecretStore::instance().read(kBioKey, &found);
        if (found)
            done(key, {});
        else
            done({}, u"Touch ID unlock isn't set up on this Mac."_s);
    });
}

void Biometric::cancel()
{
    // The pending check then answers "Cancelled" by itself.
    [g_context invalidate];
}

void Biometric::clear()
{
    SecretStore::instance().remove(kBioKey);
}

void Biometric::verifyStatus(const QString &, bool) {}
void Biometric::finishVerify(bool, const QString &) {}

} // namespace talos
