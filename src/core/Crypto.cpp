// SPDX-License-Identifier: GPL-2.0-only
#include "core/Crypto.h"

#include <QRandomGenerator>
#include <cstdlib>

extern "C" {
#include "monocypher.h"
}

namespace talos::crypto {

namespace {
constexpr int kNonceBytes = 24;
constexpr int kMacBytes = 16;

const uint8_t *u8(const QByteArray &b)
{
    return reinterpret_cast<const uint8_t *>(b.constData());
}
uint8_t *u8(QByteArray &b)
{
    return reinterpret_cast<uint8_t *>(b.data());
}
} // namespace

QByteArray randomBytes(int n)
{
    QByteArray out(n, Qt::Uninitialized);
    // QRandomGenerator::system() reads the OS CSPRNG directly.
    QRandomGenerator *rng = QRandomGenerator::system();
    for (int i = 0; i < n; ++i)
        out[i] = char(rng->bounded(256));
    return out;
}

QByteArray argon2id(const QByteArray &password, const QByteArray &salt,
                    quint32 memoryKiB, quint32 passes)
{
    crypto_argon2_config config{};
    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = qMax<quint32>(8, memoryKiB); // one block is 1 KiB
    config.nb_passes = qMax<quint32>(1, passes);
    config.nb_lanes = 1;

    crypto_argon2_inputs inputs{};
    inputs.pass = u8(password);
    inputs.pass_size = quint32(password.size());
    inputs.salt = u8(salt);
    inputs.salt_size = quint32(salt.size());

    void *work = std::malloc(size_t(config.nb_blocks) * 1024);
    if (!work)
        return {};
    QByteArray key(kKeyBytes, Qt::Uninitialized);
    crypto_argon2(u8(key), kKeyBytes, work, config, inputs, crypto_argon2_no_extras);
    std::free(work);
    return key;
}

QByteArray seal(const QByteArray &key, const QByteArray &plain, const QByteArray &ad)
{
    if (key.size() != kKeyBytes)
        return {};
    const QByteArray nonce = randomBytes(kNonceBytes);
    QByteArray out(kNonceBytes + kMacBytes + plain.size(), Qt::Uninitialized);
    std::memcpy(out.data(), nonce.constData(), kNonceBytes);
    crypto_aead_lock(u8(out) + kNonceBytes + kMacBytes, u8(out) + kNonceBytes, u8(key),
                     u8(nonce), ad.isEmpty() ? nullptr : u8(ad), size_t(ad.size()),
                     u8(plain), size_t(plain.size()));
    return out;
}

std::optional<QByteArray> open(const QByteArray &key, const QByteArray &sealed,
                               const QByteArray &ad)
{
    if (key.size() != kKeyBytes || sealed.size() < kNonceBytes + kMacBytes)
        return std::nullopt;
    const qsizetype n = sealed.size() - kNonceBytes - kMacBytes;
    QByteArray plain(n, Qt::Uninitialized);
    const int rc = crypto_aead_unlock(u8(plain), u8(sealed) + kNonceBytes, u8(key), u8(sealed),
                                      ad.isEmpty() ? nullptr : u8(ad), size_t(ad.size()),
                                      u8(sealed) + kNonceBytes + kMacBytes, size_t(n));
    if (rc != 0)
        return std::nullopt;
    return plain;
}

namespace {

const char kBoxContext[] = "talos-box-v1";

/// The symmetric key both sides of a sealed box arrive at.
QByteArray boxKey(const QByteArray &shared, const QByteArray &ephemeralPublic,
                  const QByteArray &recipientPublic)
{
    QByteArray material = shared + ephemeralPublic + recipientPublic;
    QByteArray key(kKeyBytes, Qt::Uninitialized);
    crypto_blake2b(u8(key), size_t(kKeyBytes), u8(material), size_t(material.size()));
    wipe(material);
    return key;
}

} // namespace

BoxKeys boxKeys()
{
    BoxKeys keys;
    keys.secretKey = randomBytes(kKeyBytes);
    keys.publicKey = QByteArray(kKeyBytes, Qt::Uninitialized);
    crypto_x25519_public_key(u8(keys.publicKey), u8(keys.secretKey));
    return keys;
}

QByteArray sealTo(const QByteArray &publicKey, const QByteArray &plain)
{
    if (publicKey.size() != kKeyBytes)
        return {};
    BoxKeys ephemeral = boxKeys();
    QByteArray shared(kKeyBytes, Qt::Uninitialized);
    crypto_x25519(u8(shared), u8(ephemeral.secretKey), u8(publicKey));
    QByteArray key = boxKey(shared, ephemeral.publicKey, publicKey);
    const QByteArray out = ephemeral.publicKey + seal(key, plain, kBoxContext);
    wipe(shared);
    wipe(key);
    wipe(ephemeral.secretKey);
    return out;
}

std::optional<QByteArray> openBox(const QByteArray &secretKey, const QByteArray &sealed)
{
    if (secretKey.size() != kKeyBytes || sealed.size() < kKeyBytes)
        return std::nullopt;
    const QByteArray ephemeralPublic = sealed.left(kKeyBytes);
    QByteArray publicKey(kKeyBytes, Qt::Uninitialized);
    crypto_x25519_public_key(u8(publicKey), u8(secretKey));
    QByteArray shared(kKeyBytes, Qt::Uninitialized);
    crypto_x25519(u8(shared), u8(secretKey), u8(ephemeralPublic));
    QByteArray key = boxKey(shared, ephemeralPublic, publicKey);
    const auto plain = open(key, sealed.mid(kKeyBytes), kBoxContext);
    wipe(shared);
    wipe(key);
    return plain;
}

void wipe(QByteArray &secret)
{
    if (!secret.isEmpty())
        crypto_wipe(secret.data(), size_t(secret.size()));
    secret.clear();
}

} // namespace talos::crypto
