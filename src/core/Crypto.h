// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QByteArray>
#include <optional>

/// Thin wrappers over the vendored monocypher: Argon2id for turning a
/// passcode into a key, XChaCha20-Poly1305 for sealing data under one.
namespace talos::crypto {

inline constexpr int kKeyBytes = 32;
inline constexpr int kSaltBytes = 16;

QByteArray randomBytes(int n);

/// Argon2id, one lane. @p memoryKiB of working memory, @p passes iterations.
/// Blocking and deliberately slow; call it off the UI thread.
QByteArray argon2id(const QByteArray &password, const QByteArray &salt,
                    quint32 memoryKiB, quint32 passes);

/// Seal @p plain under @p key. Output is nonce(24) | mac(16) | ciphertext.
QByteArray seal(const QByteArray &key, const QByteArray &plain, const QByteArray &ad = {});
/// Open something seal() produced; nullopt for a wrong key or tampering.
std::optional<QByteArray> open(const QByteArray &key, const QByteArray &sealed,
                               const QByteArray &ad = {});

/// A key pair for sealing *to*: anyone holding the public half can seal,
/// only the secret half opens. That lets something be put away while the
/// key that would read it back is itself locked up.
struct BoxKeys {
    QByteArray publicKey;
    QByteArray secretKey;
};
BoxKeys boxKeys();
/// Seal @p plain so only the holder of @p publicKey's secret half can open
/// it. Output is an ephemeral public key (32) followed by seal()'s output.
QByteArray sealTo(const QByteArray &publicKey, const QByteArray &plain);
std::optional<QByteArray> openBox(const QByteArray &secretKey, const QByteArray &sealed);

void wipe(QByteArray &secret);

} // namespace talos::crypto
