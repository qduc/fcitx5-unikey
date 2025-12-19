/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

// Internal text-state rebuild helpers.
//
// Historically this file implemented rebuild logic based on the application's
// SurroundingText snapshot. We no longer read SurroundingText: instead we
// maintain an internal UTF-8 buffer + cursor and rebuild from that.

#include "unikey-state.h"
#include "unikey-im.h"
#include "unikey-constants.h"
#include "unikey-log.h"
#include "unikey-utils.h"

#include "vnlexi.h"

#include <fcitx-utils/utf8.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace fcitx {

namespace {

struct RebuildItem {
    bool isAscii;
    unsigned char ascii;
    VnLexiName vn;
};

static bool isRebuildableAscii(unsigned char c) { return !isWordBreakSym(c); }

static bool isRebuildableUnicode(uint32_t unicode, RebuildItem &out) {
    if (unicode < 0x80) {
        auto c = static_cast<unsigned char>(unicode);
        if (isRebuildableAscii(c)) {
            out.isAscii = true;
            out.ascii = c;
            out.vn = vnl_nonVnChar;
            return true;
        }
        return false;
    }
    auto ch = charToVnLexi(unicode);
    if (ch != vnl_nonVnChar) {
        out.isAscii = false;
        out.ascii = 0;
        out.vn = ch;
        return true;
    }
    return false;
}

static void replayItemsToEngine(UnikeyInputContext &uic,
                                const std::vector<RebuildItem> &items,
                                UnikeyState *state,
                                std::vector<KeySym> &keyStrokes) {
    size_t itemCount = 0;
    for (const auto &item : items) {
        if (item.isAscii) {
            std::cerr << "[rebuild] Replaying ASCII: " << item.ascii << std::endl;
            uic.filter(item.ascii);
            state->syncState(static_cast<KeySym>(item.ascii));
            keyStrokes.push_back(static_cast<KeySym>(item.ascii));
        } else {
            std::cerr << "[rebuild] Replaying Vietnamese char" << std::endl;
            uic.rebuildChar(item.vn);
            state->syncState();
            // For Vietnamese chars, we don't have a single KeySym that
            // represents it as a keystroke, but we can use the Unicode value.
            // However, rebuildChar already updated the engine state.
            // If we want to support BackSpace, we might need a better way.
        }
        itemCount++;
    }
    std::cerr << "[rebuild] Replayed " << itemCount << " items" << std::endl;
}

struct InternalWord {
    std::vector<RebuildItem> items;
    size_t length = 0;          // code points
    size_t startCharacter = 0;  // code points
    // Slice into original UTF-8 (for debug only).
    std::string_view utf8Slice;
};

static InternalWord collectWordBeforeCursor(const std::string &text, size_t cursor) {
    InternalWord out;

    auto length = utf8::lengthValidated(text);
    if (length == utf8::INVALID_LENGTH) {
        return out;
    }
    if (cursor > static_cast<size_t>(length)) {
        return out;
    }
    if (cursor == 0) {
        return out;
    }

    // Only look at up to MAX_LENGTH_VNWORD code points before cursor.
    size_t startCharacter = 0;
    if (cursor >= static_cast<size_t>(MAX_LENGTH_VNWORD + 1)) {
        startCharacter = cursor - MAX_LENGTH_VNWORD - 1;
    }

    auto start = utf8::nextNChar(text.begin(), startCharacter);
    auto end = utf8::nextNChar(start, cursor - startCharacter);

    std::vector<RebuildItem> items;
    items.reserve(MAX_LENGTH_VNWORD + 1);

    auto segmentStart = start;
    size_t segmentStartChar = startCharacter;

    auto iter = start;
    size_t currentChar = startCharacter;
    while (iter != end) {
        uint32_t unicode = 0;
        auto next = utf8::getNextChar(iter, end, &unicode);
        if (unicode == utf8::INVALID_CHAR || unicode == utf8::NOT_ENOUGH_SPACE) {
            return InternalWord{};
        }

        RebuildItem item;
        const bool ok = isRebuildableUnicode(unicode, item);
        if (!ok) {
            items.clear();
            segmentStart = next;
            segmentStartChar = currentChar + 1;
        } else {
            items.push_back(item);
        }
        iter = next;
        currentChar++;
    }

    if (items.empty() || items.size() > MAX_LENGTH_VNWORD) {
        return out;
    }

    out.items = std::move(items);
    out.length = out.items.size();
    out.startCharacter = segmentStartChar;
    out.utf8Slice = std::string_view(&*segmentStart, std::distance(segmentStart, end));
    return out;
}

} // namespace

void UnikeyState::rebuildPreedit(KeySym upcomingSym) {
    // Rebuild is enabled for both immediate commit and modifySurroundingText.
    if (!*engine_->config().immediateCommit && !*engine_->config().modifySurroundingText) {
        return;
    }
    if (*engine_->config().oc != UkConv::XUTF8) {
        return;
    }

    // Only rebuild at word beginning to avoid mixing preedit-managed state
    // with arbitrary mid-word edits.
    if (!uic_.isAtWordBeginning()) {
        return;
    }

    // If user has a selection, don't attempt word rebuild/delete; allow the
    // subsequent commit to replace selection normally.
    if (internal_.hasSelection()) {
        return;
    }

    // Collect the last rebuildable segment before cursor.
    const auto word = collectWordBeforeCursor(internal_.text, internal_.cursor);
    if (word.length == 0) {
        return;
    }

    FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Internal rebuild word len=" << word.length
                         << " slice=\"" << word.utf8Slice << "\"";

    // Reset local composing buffer and engine state before rebuilding.
    uic_.resetBuf();
    preeditStr_.clear();
    keyStrokes_.clear();

    replayItemsToEngine(uic_, word.items, this, keyStrokes_);

    // Delete the old segment from the application and internal buffer.
    // (We are positioned at cursor, and the word ends at cursor.)
    deleteAroundCursor(-static_cast<int>(word.length), static_cast<int>(word.length));

    // Show rebuilt preedit so the following key modifies it.
    updatePreedit();
}

} // namespace fcitx
