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

void wipe(QByteArray &secret)
{
    if (!secret.isEmpty())
        crypto_wipe(secret.data(), size_t(secret.size()));
    secret.clear();
}

} // namespace talos::crypto
