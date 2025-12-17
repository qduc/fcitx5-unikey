/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "unikey-im.h"
#include "unikey-state.h"
#include "unikey-utils.h"
#include "unikey-constants.h"
#include "unikey-log.h"
#include "charset.h"
#include "inputproc.h"
#include "vnlexi.h"
#include <fcitx/inputcontext.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/utf8.h>
#include <vector>
#include <algorithm>
#include <cassert>
#include <string_view>

namespace fcitx {

void UnikeyState::rebuildFromSurroundingText() {
    if (mayRebuildStateFromSurroundingText_) {
        mayRebuildStateFromSurroundingText_ = false;
    } else {
        return;
    }

    // Check if output charset is utf8, otherwise it doesn't make much sense.
    // conflict with the rebuildPreedit feature
    if (!*engine_->config().surroundingText ||
        *engine_->config().oc != UkConv::XUTF8) {
        return;
    }

    if (!uic_.isAtWordBeginning()) {
        return;
    }

    if (!ic_->capabilityFlags().test(CapabilityFlag::SurroundingText) ||
        !ic_->surroundingText().isValid()) {
        return;
    }
    // We need the character before the cursor.
    const auto &text = ic_->surroundingText().text();
    auto cursor = ic_->surroundingText().cursor();
    auto length = utf8::lengthValidated(text);
    if (length == utf8::INVALID_LENGTH) {
        return;
    }
    if (cursor <= 0 || cursor > length) {
        return;
    }

    uint32_t lastCharBeforeCursor;
    auto start = utf8::nextNChar(text.begin(), cursor - 1);
    auto end = utf8::getNextChar(start, text.end(), &lastCharBeforeCursor);
    if (lastCharBeforeCursor == utf8::INVALID_CHAR ||
        lastCharBeforeCursor == utf8::NOT_ENOUGH_SPACE) {
        return;
    }

    const auto isValidStateCharacter = [](char c) {
        return isWordAutoCommit(c) && !charutils::isdigit(c);
    };

    if (std::distance(start, end) != 1 ||
        !isValidStateCharacter(lastCharBeforeCursor)) {
        return;
    }

    // Reverse search for word auto commit.
    // all char for isWordAutoCommit == true would be ascii.
    while (start != text.begin() && isValidStateCharacter(*start) &&
           std::distance(start, end) < MAX_LENGTH_VNWORD) {
        --start;
    }

    // The loop will move the character on to an invalid character, if it
    // doesn't by pass the start point. Need to add by one to move it to the
    // starting point we expect.
    if (!isValidStateCharacter(*start)) {
        ++start;
    }

    assert(isValidStateCharacter(*start) && start >= text.begin());

    // Check if surrounding is not in a bigger part of word.
    if (start != text.begin()) {
        auto chr = utf8::getLastChar(text.begin(), start);
        if (isVnChar(chr)) {
            return;
        }
    }

    FCITX_UNIKEY_DEBUG()
        << "Rebuild surrounding with: \""
        << std::string_view(&*start, std::distance(start, end)) << "\"";
    for (; start != end; ++start) {
        uic_.putChar(*start);
        autoCommit_ = true;
    }
}

size_t UnikeyState::rebuildStateFromSurrounding(bool deleteSurrounding) {
    // Ask the frontend to refresh surrounding text so we can see what was
    // just committed.
    ic_->updateSurroundingText();

    // If there is an active selection, avoid rebuild/delete/recommit logic.
    // The application will typically replace the selection on commit, and
    // rebuilding would corrupt surrounding text.
    // However, if the selection is purely forward (e.g. auto-suggestion),
    // we can still safely modify the text before the cursor.
    if (ic_->surroundingText().isValid() &&
        !ic_->surroundingText().selectedText().empty()) {
        auto cursor = ic_->surroundingText().cursor();
        auto anchor = ic_->surroundingText().anchor();
        if (std::min(cursor, anchor) < cursor) {
            return 0;
        }
    }

    if (!ic_->surroundingText().isValid()) {
        // If surrounding text is unavailable, skip rebuild to avoid corrupting
        // text.
        return 0;
    }

    // Rebuild from the last word (already committed) before the cursor.
    // We'll delete it during commit and re-commit the transformed result.
    const auto &text = ic_->surroundingText().text();
    auto cursor = ic_->surroundingText().cursor();
    auto length = utf8::lengthValidated(text);
    if (length == utf8::INVALID_LENGTH) {
        return 0;
    }
    if (cursor > length) {
        return 0;
    }

    // Collect last contiguous "word" before cursor with a length cap.
    // - ASCII characters are treated as part of the word as long as they're
    //   not a word-break symbol.
    // - Non-ASCII characters must be Vietnamese letters understood by vnlexi.
    struct RebuildItem {
        bool isAscii;
        unsigned char ascii;
        VnLexiName vn;
    };

    std::vector<RebuildItem> items;
    items.reserve(MAX_LENGTH_VNWORD + 1);

    size_t startCharacter = 0;
    if (cursor >= static_cast<decltype(cursor)>(MAX_LENGTH_VNWORD + 1)) {
        startCharacter = cursor - MAX_LENGTH_VNWORD - 1;
    }

    auto start = utf8::nextNChar(text.begin(), startCharacter);
    auto end = utf8::nextNChar(start, cursor - startCharacter);

    auto segmentStart = start;
    auto iter = start;
    while (iter != end) {
        uint32_t unicode = 0;
        auto next = utf8::getNextChar(iter, end, &unicode);
        if (unicode == utf8::INVALID_CHAR || unicode == utf8::NOT_ENOUGH_SPACE) {
            return 0;
        }

        bool ok = false;
        RebuildItem item;
        if (unicode < 0x80) {
            auto c = static_cast<unsigned char>(unicode);
            if (!isWordBreakSym(c)) {
                ok = true;
                item.isAscii = true;
                item.ascii = c;
                item.vn = vnl_nonVnChar;
            }
        } else {
            auto ch = charToVnLexi(unicode);
            if (ch != vnl_nonVnChar) {
                ok = true;
                item.isAscii = false;
                item.ascii = 0;
                item.vn = ch;
            }
        }

        if (!ok) {
            items.clear();
            segmentStart = next;
        } else {
            items.push_back(item);
        }
        iter = next;
    }

    const size_t wordLength = items.size();
    if (wordLength == 0 || wordLength > MAX_LENGTH_VNWORD) {
        return 0;
    }

    // Reset local composing buffer and engine state before rebuilding.
    uic_.resetBuf();
    preeditStr_.clear();

    // Rebuild ukengine state and our composing string by replaying the
    // current word. For ASCII characters we need filtering, otherwise the
    // engine won't recognize sequences like "aa" -> "â".
    for (const auto &item : items) {
        if (item.isAscii) {
            uic_.filter(item.ascii);
            syncState(static_cast<KeySym>(item.ascii));
        } else {
            uic_.rebuildChar(item.vn);
            syncState();
        }
    }

    if (deleteSurrounding) {
        ic_->deleteSurroundingText(-static_cast<int>(wordLength),
                                   static_cast<int>(wordLength));
    }
    return wordLength;
}

void UnikeyState::rebuildPreedit() {
    // Also enable this path for immediate commit.
    if (!immediateCommitMode()) {
        return;
    }

    if (*engine_->config().oc != UkConv::XUTF8) {
        return;
    }

    if (!uic_.isAtWordBeginning()) {
        return;
    }

    if (rebuildStateFromSurrounding(true)) {
        updatePreedit();
    }
}

} // namespace fcitx
