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

void wipe(QByteArray &secret);

} // namespace talos::crypto
