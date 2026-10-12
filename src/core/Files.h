// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>

namespace talos {

/// Move @p from to @p to, replacing a file already there. Where the system
/// can do that in one step it does, so a move that fails leaves the old
/// file as it was instead of gone.
bool moveOver(const QString &from, const QString &to);

} // namespace talos
