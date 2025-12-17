/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef UNIKEY_UTILS_H
#define UNIKEY_UTILS_H

#include <cstdint>
#include "vnlexi.h"

namespace fcitx {

bool isWordBreakSym(unsigned char c);

bool isWordAutoCommit(unsigned char c);

VnLexiName charToVnLexi(uint32_t ch);

bool isVnChar(uint32_t ch);

// Convert Latin charset to UTF-8
// Returns true if conversion was successful, false if output buffer was too small
int latinToUtf(unsigned char *dst, const unsigned char *src, int inSize,
               int *pOutSize);

} // namespace fcitx

#endif // UNIKEY_UTILS_H
