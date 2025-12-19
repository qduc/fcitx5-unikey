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

// Probe the last contiguous "word" before cursor, without mutating UnikeyState.
// This is used to detect when surrounding text becomes reliable again while we
// are in the "unreliable" state (where we must not rewrite/delete text).
static size_t probeWordLengthFromSurrounding(const SurroundingText &st) {
    if (!st.isValid() || !st.selectedText().empty()) {
        return 0;
    }

    const auto &text = st.text();
    auto cursor = st.cursor();
    auto length = utf8::lengthValidated(text);
    if (length == utf8::INVALID_LENGTH) {
        return 0;
    }
    if (cursor > length) {
        return 0;
    }

    // Collect last contiguous rebuildable segment before cursor.
    size_t startCharacter = 0;
    if (cursor >= static_cast<decltype(cursor)>(MAX_LENGTH_VNWORD + 1)) {
        startCharacter = cursor - MAX_LENGTH_VNWORD - 1;
    }

    auto start = utf8::nextNChar(text.begin(), startCharacter);
    auto end = utf8::nextNChar(start, cursor - startCharacter);

    std::vector<RebuildItem> items;
    items.reserve(MAX_LENGTH_VNWORD + 1);

    auto iter = start;
    while (iter != end) {
        uint32_t unicode = 0;
        auto next = utf8::getNextChar(iter, end, &unicode);
        if (unicode == utf8::INVALID_CHAR || unicode == utf8::NOT_ENOUGH_SPACE) {
            return 0;
        }

        RebuildItem item;
        bool ok = isRebuildableUnicode(unicode, item);
        if (!ok) {
            items.clear();
        } else {
            items.push_back(item);
        }
        iter = next;
    }

    const size_t wordLength = items.size();
    if (wordLength == 0 || wordLength > MAX_LENGTH_VNWORD) {
        return 0;
    }
    return wordLength;
}

} // namespace

void UnikeyState::rebuildFromSurroundingText() {
    if (mayRebuildStateFromSurroundingText_) {
        mayRebuildStateFromSurroundingText_ = false;
    } else {
        return;
    }

    // Check if output charset is utf8, otherwise it doesn't make much sense.
    // conflict with the rebuildPreedit feature
    if (!*engine_->config().surroundingText ||
        *engine_->config().modifySurroundingText) {
        FCITX_UNIKEY_DEBUG() << "[rebuildFromSurroundingText] Surrounding text "
                                "disabled or modifySurroundingText enabled";
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
    std::cerr << "[rebuildStateFromSurrounding] Called with deleteSurrounding=" << deleteSurrounding << std::endl;

    // Reset transient stale marker for this attempt.
    lastSurroundingRebuildWasStale_ = false;

    // Ask the frontend to refresh surrounding text so we can see what was
    // just committed.
    ic_->updateSurroundingText();

    // If there is an active selection, avoid rebuild/delete/recommit logic.
    // The application will typically replace the selection on commit, and
    // rebuilding would corrupt surrounding text.
    if (ic_->surroundingText().isValid() &&
        !ic_->surroundingText().selectedText().empty()) {
        return 0;
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
    std::cerr << "[rebuildStateFromSurrounding] Text: \"" << text << "\" cursor: " << cursor << " lastImmediateWord: \"" << lastImmediateWord_ << "\"" << std::endl;
    auto length = utf8::lengthValidated(text);

    // If we have a recent immediate-commit word but the app reports completely
    // empty surrounding text, it is very likely a stale snapshot (observed in
    // some browsers). Mark as stale so rebuildPreedit() can try the
    // lastImmediateWord_ fallback.
    if (deleteSurrounding && !lastImmediateWord_.empty() && text.empty()) {
        FCITX_UNIKEY_DEBUG()
            << "[rebuildStateFromSurrounding] Surrounding text empty while lastImmediateWord=\""
            << lastImmediateWord_ << "\", treating as stale";
        lastSurroundingRebuildWasStale_ = true;
        return 0;
    }

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

        RebuildItem item;
        bool ok = isRebuildableUnicode(unicode, item);

        if (!ok) {
            items.clear();
            segmentStart = next;
        } else {
            items.push_back(item);
        }
        iter = next;
    }

    const size_t wordLength = items.size();
    std::cerr << "[rebuildStateFromSurrounding] Collected word length: " << wordLength << std::endl;

    if (wordLength == 0 || wordLength > MAX_LENGTH_VNWORD) {
        std::cerr << "[rebuildStateFromSurrounding] Word length invalid, skipping" << std::endl;
        return 0;
    }

    // Safety: if we have a last immediate-commit word, but the app's surrounding
    // text doesn't match it, the surrounding text is likely stale (e.g. Firefox
    // after commit). In that case, do NOT delete/rebuild from surrounding.
    // We'll try a safer fallback path in rebuildPreedit().
    if (deleteSurrounding && !lastImmediateWord_.empty()) {
        const std::string wordUtf8(&*segmentStart, std::distance(segmentStart, end));
        if (wordUtf8.empty()) {
            FCITX_UNIKEY_DEBUG()
                << "[rebuildStateFromSurrounding] Surrounding word empty while lastImmediateWord=\""
                << lastImmediateWord_ << "\", treating as stale";
            lastSurroundingRebuildWasStale_ = true;
            return 0;
        }

        if (wordUtf8 != lastImmediateWord_) {
            // Detect stale surrounding snapshots that are likely *truncated*
            // versions of the last committed word (common in Firefox timing
            // issues). In that case, rebuilding/deleting based on surrounding
            // may delete too little and corrupt text.
            const bool lastStartsWithSurrounding =
                lastImmediateWord_.size() > wordUtf8.size() &&
                lastImmediateWord_.compare(0, wordUtf8.size(), wordUtf8) == 0;
            const bool lastEndsWithSurrounding =
                lastImmediateWord_.size() > wordUtf8.size() &&
                lastImmediateWord_.compare(lastImmediateWord_.size() - wordUtf8.size(),
                                           wordUtf8.size(), wordUtf8) == 0;

            if (lastStartsWithSurrounding || lastEndsWithSurrounding) {
                FCITX_UNIKEY_DEBUG()
                    << "[rebuildStateFromSurrounding] Surrounding word looks truncated (got=\""
                    << wordUtf8 << "\", last=\"" << lastImmediateWord_
                    << "\"), treating as stale";
                lastSurroundingRebuildWasStale_ = true;
                return 0;
            }

            // If surrounding is longer but ends with lastImmediateWord_, trust
            // surrounding (it has more context, important for tone placement).
            const bool surroundingEndsWithLast =
                wordUtf8.size() > lastImmediateWord_.size() &&
                wordUtf8.compare(wordUtf8.size() - lastImmediateWord_.size(),
                                 lastImmediateWord_.size(),
                                 lastImmediateWord_) == 0;
            if (surroundingEndsWithLast) {
                FCITX_UNIKEY_DEBUG()
                    << "[rebuildStateFromSurrounding] Surrounding word has extra prefix (got=\""
                    << wordUtf8 << "\", last=\"" << lastImmediateWord_
                    << "\"), accepting surrounding";
            } else {
                // Otherwise, assume the user moved the cursor / changed context
                // and the surrounding word is authoritative.
                FCITX_UNIKEY_DEBUG()
                    << "[rebuildStateFromSurrounding] Surrounding word differs from lastImmediateWord (got=\""
                    << wordUtf8 << "\", last=\"" << lastImmediateWord_
                    << "\"), accepting surrounding";
            }
        }
    }

    // Reset local composing buffer and engine state before rebuilding.
    FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Resetting engine and preedit, rebuilding word";
    uic_.resetBuf();
    preeditStr_.clear();
    keyStrokes_.clear();

    // Rebuild ukengine state and our composing string by replaying the
    // current word. For ASCII characters we need filtering, otherwise the
    // engine won't recognize sequences like "aa" -> "â".
    replayItemsToEngine(uic_, items, this, keyStrokes_);

    if (deleteSurrounding) {
        FCITX_UNIKEY_DEBUG() << "[rebuildStateFromSurrounding] Deleting surrounding text: -"
                             << wordLength << " " << wordLength;
        ic_->deleteSurroundingText(-static_cast<int>(wordLength),
                                   static_cast<int>(wordLength));
    }
    return wordLength;
}

size_t UnikeyState::rebuildStateFromLastImmediateWord(bool deleteSurrounding, KeySym upcomingSym) {
    if (lastImmediateWord_.empty() || lastImmediateWordCharCount_ == 0 ||
        lastImmediateWordCharCount_ > MAX_LENGTH_VNWORD) {
        return 0;
    }

    // Parse the last immediate word into rebuild items.
    std::vector<RebuildItem> items;
    items.reserve(lastImmediateWordCharCount_ + 1);

    auto it = lastImmediateWord_.begin();
    auto end = lastImmediateWord_.end();
    while (it != end) {
        uint32_t unicode = 0;
        auto next = utf8::getNextChar(it, end, &unicode);
        if (unicode == utf8::INVALID_CHAR || unicode == utf8::NOT_ENOUGH_SPACE) {
            return 0;
        }
        RebuildItem item;
        if (!isRebuildableUnicode(unicode, item)) {
            return 0;
        }
        items.push_back(item);
        it = next;
    }

    if (items.empty()) {
        return 0;
    }

    FCITX_UNIKEY_DEBUG() << "[rebuildStateFromLastImmediateWord] Rebuilding from \""
                         << lastImmediateWord_ << "\" items=" << items.size();

    // Reset local composing buffer and engine state before rebuilding.
    uic_.resetBuf();
    preeditStr_.clear();
    keyStrokes_.clear();

    replayItemsToEngine(uic_, items, this, keyStrokes_);

    if (deleteSurrounding) {
        FCITX_UNIKEY_DEBUG()
            << "[rebuildStateFromLastImmediateWord] Deleting surrounding text: -"
            << lastImmediateWordCharCount_ << " " << lastImmediateWordCharCount_;
        ic_->deleteSurroundingText(-static_cast<int>(lastImmediateWordCharCount_),
                                   static_cast<int>(lastImmediateWordCharCount_));
    }

    return items.size();
}

void UnikeyState::rebuildPreedit(KeySym upcomingSym) {
    std::cerr << "[rebuildPreedit] Called upcomingSym=" << upcomingSym << std::endl;

    // Also enable this path for immediate commit.
    // NOTE: When surroundingTextUnreliable_ is true, immediateCommitMode() is
    // intentionally disabled. We still want to *probe* surrounding text to
    // allow recovery, but we must not rewrite/delete surrounding nor mutate
    // composing state.
    if (!*engine_->config().immediateCommit && !*engine_->config().modifySurroundingText) {
        std::cerr << "[rebuildPreedit] Disabled by config" << std::endl;
        return;
    }

    // For immediateCommit mode without modifySurroundingText, we need reliable
    // surrounding text from the app. Skip for known problematic apps like Firefox.
    // However, modifySurroundingText mode can work even with unreliable apps
    // because it only rebuilds the preedit buffer from cursor position, not
    // performing direct rewrites of committed text.
    if (isUnsupportedSurroundingApp() && !*engine_->config().modifySurroundingText) {
        std::cerr << "[rebuildPreedit] Disabled for unsupported app (immediateCommit mode only)" << std::endl;
        return;
    }

    if (*engine_->config().oc != UkConv::XUTF8) {
        std::cerr << "[rebuildPreedit] Output charset is not XUTF8" << std::endl;
        return;
    }

    bool atWordBeginning = uic_.isAtWordBeginning();
    std::cerr << "[rebuildPreedit] isAtWordBeginning=" << atWordBeginning << std::endl;
    if (!atWordBeginning) {
        std::cerr << "[rebuildPreedit] Not at word beginning" << std::endl;
        return;
    }

    bool hasSurrounding = ic_->capabilityFlags().test(CapabilityFlag::SurroundingText);
    std::cerr << "[rebuildPreedit] hasSurrounding=" << hasSurrounding << " unreliable=" << surroundingTextUnreliable_ << std::endl;
    if (!hasSurrounding) {
        std::cerr << "[rebuildPreedit] SurroundingText capability not available" << std::endl;
        return;
    }

    // Recovery probe: if we're currently in unreliable mode, do not attempt
    // to rebuild/delete/modify state. Only probe the surrounding snapshot and
    // count consecutive successes to recover.
    if (surroundingTextUnreliable_) {
        // Even in unreliable mode, we can still support VNI tone/shape keys
        // by rebuilding from our own lastImmediateWord_ history (instead of
        // trusting the application's surrounding snapshot). This enables fast
        // typing scenarios where surrounding updates lag behind.
        if (*engine_->config().immediateCommit &&
            *engine_->config().im == UkVni &&
            !lastImmediateWord_.empty()) {
            const bool isDigit =
                (upcomingSym >= FcitxKey_0 && upcomingSym <= FcitxKey_9) ||
                (upcomingSym >= FcitxKey_KP_0 && upcomingSym <= FcitxKey_KP_9);
            if (isDigit) {
                FCITX_UNIKEY_DEBUG()
                    << "[rebuildPreedit] Unreliable: VNI digit key, rebuilding from lastImmediateWord";
                const size_t fallbackLen =
                    rebuildStateFromLastImmediateWord(true, upcomingSym);
                if (fallbackLen > 0) {
                    updatePreedit();
                    return;
                }
            }
        }

        ic_->updateSurroundingText();
        const size_t probeLen = probeWordLengthFromSurrounding(ic_->surroundingText());
        if (probeLen > 0) {
            surroundingSuccessCount_++;
            surroundingFailureCount_ = 0;
            FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Unreliable: probe success len="
                                 << probeLen << " success=" << surroundingSuccessCount_
                                 << "/" << kSurroundingRecoveryThreshold;
            if (surroundingSuccessCount_ >= kSurroundingRecoveryThreshold) {
                FCITX_UNIKEY_DEBUG()
                    << "[rebuildPreedit] Recovery threshold reached; clearing unreliable flag";
                surroundingTextUnreliable_ = false;
                surroundingSuccessCount_ = 0;
            }
        } else {
            // Break success streak if surrounding is still not usable.
            surroundingSuccessCount_ = 0;
            FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Unreliable: probe failed";
        }
        return;
    }

    std::cerr << "[rebuildPreedit] Attempting to rebuild from surrounding" << std::endl;
    size_t wordLen = rebuildStateFromSurrounding(true);
    if (wordLen > 0) {
        std::cerr << "[rebuildPreedit] Rebuilt " << wordLen
                             << " chars from surrounding, updating preedit" << std::endl;
        // Successful rebuild: track success for potential recovery.
        surroundingSuccessCount_++;
        surroundingFailureCount_ = 0;  // Reset failure streak.
        if (surroundingTextUnreliable_ &&
            surroundingSuccessCount_ >= kSurroundingRecoveryThreshold) {
            FCITX_UNIKEY_DEBUG()
                << "[rebuildPreedit] Surrounding text has been reliable for "
                << surroundingSuccessCount_
                << " operations, recovering from unreliable state";
            surroundingTextUnreliable_ = false;
            surroundingSuccessCount_ = 0;
        }
        updatePreedit();
        return;
    }

    // If surrounding rebuild failed but we have a recent immediate-commit word,
    // this is likely an app (e.g. Firefox) returning stale/empty surrounding
    // right after commit. Use the last committed word as a safer rewrite
    // source.
    if (lastSurroundingRebuildWasStale_ && !lastImmediateWord_.empty()) {
        FCITX_UNIKEY_DEBUG()
            << "[rebuildPreedit] Surrounding rebuild failed; trying lastImmediateWord fallback";
        size_t fallbackLen = rebuildStateFromLastImmediateWord(true, upcomingSym);
        if (fallbackLen > 0) {
            FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] Fallback rebuilt "
                                 << fallbackLen << " chars, updating preedit";
            // Fallback succeeded, but this still indicates surrounding was
            // stale. Count as a failure for the detection logic.
            surroundingFailureCount_++;
            surroundingSuccessCount_ = 0;
            FCITX_UNIKEY_DEBUG()
                << "[rebuildPreedit] Surrounding failure count: "
                << surroundingFailureCount_ << "/" << kSurroundingFailureThreshold;

            // Even if internal fallback succeeds, repeated stale surrounding
            // snapshots still mean surrounding is unreliable for immediate
            // commit (we cannot safely delete/replace around cursor).
            if (surroundingFailureCount_ >= kSurroundingFailureThreshold) {
                FCITX_UNIKEY_DEBUG()
                    << "[rebuildPreedit] Failure threshold reached (stale surrounding); marking unreliable";
                surroundingTextUnreliable_ = true;
            }
            updatePreedit();
            return;
        }

        // Both surrounding and internal fallback failed. Increment failure
        // count and only mark as unreliable after reaching the threshold.
        surroundingFailureCount_++;
        surroundingSuccessCount_ = 0;
        FCITX_UNIKEY_DEBUG()
            << "[rebuildPreedit] Both surrounding and fallback failed; failure count: "
            << surroundingFailureCount_ << "/" << kSurroundingFailureThreshold;

        if (surroundingFailureCount_ >= kSurroundingFailureThreshold) {
            FCITX_UNIKEY_DEBUG()
                << "[rebuildPreedit] Failure threshold reached; marking surrounding unreliable";
            surroundingTextUnreliable_ = true;
        }
    } else if (lastSurroundingRebuildWasStale_ && lastImmediateWord_.empty()) {
        // Staleness detected but no fallback word available. This could
        // indicate a problematic app even without lastImmediateWord_. Count
        // it as a failure.
        surroundingFailureCount_++;
        surroundingSuccessCount_ = 0;
        FCITX_UNIKEY_DEBUG()
            << "[rebuildPreedit] Surrounding stale but no fallback word; failure count: "
            << surroundingFailureCount_ << "/" << kSurroundingFailureThreshold;

        if (surroundingFailureCount_ >= kSurroundingFailureThreshold) {
            FCITX_UNIKEY_DEBUG()
                << "[rebuildPreedit] Failure threshold reached; marking surrounding unreliable";
            surroundingTextUnreliable_ = true;
        }
    } else {
        FCITX_UNIKEY_DEBUG() << "[rebuildPreedit] No word rebuilt (no prior immediate word, not stale)";
    }
}

} // namespace fcitx
