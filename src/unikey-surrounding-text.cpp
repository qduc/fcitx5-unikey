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
    FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Called, flag=" << mayRebuildStateFromSurroundingText_;

    if (mayRebuildStateFromSurroundingText_) {
        mayRebuildStateFromSurroundingText_ = false;
    } else {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Flag not set, returning";
        return;
    }

    // Check if output charset is utf8, otherwise it doesn't make much sense.
    // conflict with the rebuildPreedit feature
    if (!*engine_->config().surroundingText) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Surrounding text disabled";
        return;
    }

    if (*engine_->config().oc != UkConv::XUTF8) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Surrounding text "
                                "is not XUTF8";
        return;
    }

    if (!uic_.isAtWordBeginning()) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Not at word beginning";
        return;
    }

    if (!ic_->capabilityFlags().test(CapabilityFlag::SurroundingText) ||
        !ic_->surroundingText().isValid()) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] SurroundingText capability not available or invalid";
        return;
    }

    // If there is an active selection, avoid rebuilding state.
    // The application will typically replace the selection on commit, and
    // rebuilding would corrupt surrounding text or cause double characters.
    if (!ic_->surroundingText().selectedText().empty()) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Text selected, avoiding rebuild";
        return;
    }

    // We need the character before the cursor.
    const auto &text = ic_->surroundingText().text();
    auto cursor = ic_->surroundingText().cursor();
    auto length = utf8::lengthValidated(text);
    FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Text: \"" << text
                         << "\" cursor: " << cursor << " length: " << length;

    if (length == utf8::INVALID_LENGTH) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Invalid UTF8 length";
        return;
    }
    if (cursor <= 0 || cursor > length) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Cursor out of range";
        return;
    }

    uint32_t lastCharBeforeCursor;
    auto start = utf8::nextNChar(text.begin(), cursor - 1);
    auto end = utf8::getNextChar(start, text.end(), &lastCharBeforeCursor);
    if (lastCharBeforeCursor == utf8::INVALID_CHAR ||
        lastCharBeforeCursor == utf8::NOT_ENOUGH_SPACE) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Invalid char before cursor";
        return;
    }

    const auto isValidStateCharacter = [](char c) {
        return isWordAutoCommit(c) && !charutils::isdigit(c);
    };

    if (std::distance(start, end) != 1 ||
        !isValidStateCharacter(lastCharBeforeCursor)) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Last char not valid for auto commit";
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
            FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Part of Vietnamese word, skipping";
            return;
        }
    }

    FCITX_UNIKEY_DEBUG()
        << "[rebuildFromSurroundingText] Rebuild surrounding with: \""
        << std::string_view(&*start, std::distance(start, end)) << "\"";
    for (; start != end; ++start) {
        uic_.putChar(*start);
        autoCommit_ = true;
    }
}

size_t UnikeyState::rebuildStateFromSurrounding(bool deleteSurrounding) {
    FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Called with deleteSurrounding=" << deleteSurrounding;

    // Ask the frontend to refresh surrounding text so we can see what was
    // just committed.
    ic_->updateSurroundingText();

    // If there is an active selection, avoid rebuild/delete/recommit logic.
    // The application will typically replace the selection on commit, and
    // rebuilding would corrupt surrounding text.
    if (ic_->surroundingText().isValid() &&
        !ic_->surroundingText().selectedText().empty()) {
        FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Text selected, skipping rebuild";
        return 0;
    }

    if (!ic_->surroundingText().isValid()) {
        // If surrounding text is unavailable, skip rebuild to avoid corrupting
        // text.
        FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Surrounding text invalid";
        return 0;
    }

    // Rebuild from the last word (already committed) before the cursor.
    // We'll delete it during commit and re-commit the transformed result.
    const auto &text = ic_->surroundingText().text();
    auto cursor = ic_->surroundingText().cursor();
    auto length = utf8::lengthValidated(text);
    FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Text: \"" << text
                         << "\" cursor: " << cursor << " length: " << length;

    if (length == utf8::INVALID_LENGTH) {
        FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Invalid UTF8 length";
        return 0;
    }
    if (cursor > length) {
        FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Cursor beyond text length";
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
    FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Collected word length: " << wordLength;

    if (wordLength == 0 || wordLength > MAX_LENGTH_VNWORD) {
        FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Word length invalid, skipping";
        return 0;
    }

    // Reset local composing buffer and engine state before rebuilding.
    FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Resetting engine and preedit, rebuilding word";
    uic_.resetBuf();
    preeditStr_.clear();

    // Rebuild ukengine state and our composing string by replaying the
    // current word. For ASCII characters we need filtering, otherwise the
    // engine won't recognize sequences like "aa" -> "â".
    size_t itemCount = 0;
    for (const auto &item : items) {
        if (item.isAscii) {
            FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Replaying ASCII: " << item.ascii;
            uic_.filter(item.ascii);
            syncState(static_cast<KeySym>(item.ascii));
        } else {
            FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Replaying Vietnamese char";
            uic_.rebuildChar(item.vn);
            syncState();
        }
        itemCount++;
    }

    FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Rebuilt preedit: \"" << preeditStr_
                         << "\" from " << itemCount << " items";

    if (deleteSurrounding) {
        FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Deleting surrounding text: -"
                             << wordLength << " " << wordLength;
        ic_->deleteSurroundingText(-static_cast<int>(wordLength),
                                   static_cast<int>(wordLength));
    }
    return wordLength;
}

void UnikeyState::rebuildPreedit() {
    FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Called";

    // Also enable this path for immediate commit.
    if (!immediateCommitMode()) {
        FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Immediate commit mode not available";
        return;
    }

    if (*engine_->config().oc != UkConv::XUTF8) {
        FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Output charset is not XUTF8";
        return;
    }

    if (!uic_.isAtWordBeginning()) {
        FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Not at word beginning";
        return;
    }

    FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Attempting to rebuild from surrounding";
    size_t wordLen = rebuildStateFromSurrounding(true);
    if (wordLen > 0) {
        FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Rebuilt " << wordLen << " chars, updating preedit";
        updatePreedit();
    } else {
        FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] No word rebuilt";
    }
}

} // namespace fcitx
