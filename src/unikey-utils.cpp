/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "unikey-utils.h"
#include "charset.h"
#include "inputproc.h"
#include "vnlexi.h"
#include <unordered_map>
#include <unordered_set>

namespace fcitx {

bool isWordBreakSym(unsigned char c) { return WordBreakSyms.contains(c); }

bool isWordAutoCommit(unsigned char c) {
    static const std::unordered_set<unsigned char> WordAutoCommit = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'b', 'c',
        'f', 'g', 'h', 'j', 'k', 'l', 'm', 'n', 'p', 'q', 'r', 's',
        't', 'v', 'x', 'z', 'B', 'C', 'F', 'G', 'H', 'J', 'K', 'L',
        'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'V', 'X', 'Z'};
    return WordAutoCommit.contains(c);
}

VnLexiName charToVnLexi(uint32_t ch) {
    static const std::unordered_map<uint32_t, VnLexiName> map = []() {
        std::unordered_map<uint32_t, VnLexiName> result;
        for (int i = 0; i < vnl_lastChar; i++) {
            result.insert({UnicodeTable[i], static_cast<VnLexiName>(i)});
        }
        return result;
    }();

    if (auto search = map.find(ch); search != map.end()) {
        return search->second;
    }
    return vnl_nonVnChar;
}

bool isVnChar(uint32_t ch) { return charToVnLexi(ch) != vnl_nonVnChar; }

// code from x-unikey, for convert charset that not is XUtf-8
int latinToUtf(unsigned char *dst, const unsigned char *src, int inSize,
               int *pOutSize) {
    int i;
    int outLeft;
    unsigned char ch;

    outLeft = *pOutSize;

    for (i = 0; i < inSize; i++) {
        ch = *src++;
        if (ch < 0x80) {
            outLeft -= 1;
            if (outLeft >= 0) {
                *dst++ = ch;
            }
        } else {
            outLeft -= 2;
            if (outLeft >= 0) {
                *dst++ = (0xC0 | ch >> 6);
                *dst++ = (0x80 | (ch & 0x3F));
            }
        }
    }

    *pOutSize = outLeft;
    return (outLeft >= 0);
}

} // namespace fcitx
