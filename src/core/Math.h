// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>

namespace talos::math {

/// LaTeX mathematics as readable Unicode text.
///
/// This is not typesetting. There is no TeX engine in the app, so a formula
/// is turned into the nearest thing plain characters can say: `\alpha^2 +
/// \frac{1}{n}` becomes `α² + 1/n`. Greek letters, the common operators and
/// relations, fractions, roots, and super- and subscripts are covered;
/// anything unknown is left as its name, without the backslash.
QString toUnicode(const QString &tex);

} // namespace talos::math
