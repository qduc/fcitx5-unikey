/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "unikey-state.h"
#include "unikey-im.h"
#include "charset.h"
#include "inputproc.h"
#include "keycons.h"
#include "unikey-config.h"
#include "unikeyinputcontext.h"
#include "usrkeymap.h"
#include "unikey-utils.h"
#include "unikey-constants.h"
#include "unikey-log.h"
#include "vnconv.h"
#include "vnlexi.h"
#include <cassert>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

#include <iostream>
namespace fcitx {

size_t UnikeyState::InternalTextState::length() const {
    auto len = utf8::lengthValidated(text);
    if (len == utf8::INVALID_LENGTH) {
        return 0;
    }
    return static_cast<size_t>(len);
}

void UnikeyState::InternalTextState::clampCursor() {
    const auto len = length();
    if (cursor > len) {
        cursor = len;
    }
    if (selectionAnchor && *selectionAnchor > len) {
        // Keep anchor in range, but preserve selection direction.
        *selectionAnchor = len;
    }
}

void UnikeyState::InternalTextState::eraseRange(size_t start, size_t end) {
    const auto len = length();
    if (start > end) {
        std::swap(start, end);
    }
    start = std::min(start, len);
    end = std::min(end, len);
    if (start == end) {
        return;
    }

    auto itStart = utf8::nextNChar(text.begin(), start);
    auto itEnd = utf8::nextNChar(itStart, end - start);
    text.erase(itStart, itEnd);

    // Adjust cursor/anchor.
    if (cursor > end) {
        cursor -= (end - start);
    } else if (cursor > start) {
        cursor = start;
    }

    if (selectionAnchor) {
        auto &a = *selectionAnchor;
        if (a > end) {
            a -= (end - start);
        } else if (a > start) {
            a = start;
        }
        if (a == cursor) {
            selectionAnchor.reset();
        }
    }
}

void UnikeyState::InternalTextState::insertText(const std::string &utf8Text) {
    // Replace selection.
    if (hasSelection()) {
        const auto [s, e] = selectionRange();
        eraseRange(s, e);
        clearSelection();
    }

    clampCursor();
    auto it = utf8::nextNChar(text.begin(), cursor);
    text.insert(it, utf8Text.begin(), utf8Text.end());

    // Advance cursor by inserted code points.
    auto add = utf8::lengthValidated(utf8Text);
    if (add != utf8::INVALID_LENGTH) {
        cursor += static_cast<size_t>(add);
    } else {
        // Fallback: re-clamp.
        clampCursor();
    }
}

void UnikeyState::InternalTextState::backspace() {
    if (hasSelection()) {
        const auto [s, e] = selectionRange();
        eraseRange(s, e);
        clearSelection();
        return;
    }
    if (cursor == 0) {
        return;
    }
    eraseRange(cursor - 1, cursor);
}

void UnikeyState::InternalTextState::del() {
    if (hasSelection()) {
        const auto [s, e] = selectionRange();
        eraseRange(s, e);
        clearSelection();
        return;
    }
    const auto len = length();
    if (cursor >= len) {
        return;
    }
    eraseRange(cursor, cursor + 1);
}

static bool isWordChar(uint32_t ch) {
    if (ch < 0x80) {
        const auto c = static_cast<unsigned char>(ch);
        if (charutils::isspace(c)) {
            return false;
        }
        // Treat typical word-break symbols as separators.
        return !isWordBreakSym(c);
    }
    // Treat non-ASCII as word chars (Vietnamese letters, etc.).
    return true;
}

void UnikeyState::InternalTextState::moveLeft(bool byWord, bool extendSelection) {
    clampCursor();
    if (!extendSelection) {
        clearSelection();
    } else if (!selectionAnchor) {
        selectionAnchor = cursor;
    }

    if (cursor == 0) {
        return;
    }
    if (!byWord) {
        cursor -= 1;
        return;
    }

    // Word move: skip separators, then skip word chars.
    auto it = utf8::nextNChar(text.begin(), cursor);
    // Move one char left first.
    uint32_t ch = utf8::getLastChar(text.begin(), it);
    cursor -= 1;

    // Skip separators.
    while (cursor > 0) {
        it = utf8::nextNChar(text.begin(), cursor);
        ch = utf8::getLastChar(text.begin(), it);
        if (isWordChar(ch)) {
            break;
        }
        cursor -= 1;
    }
    // Skip word chars.
    while (cursor > 0) {
        it = utf8::nextNChar(text.begin(), cursor);
        ch = utf8::getLastChar(text.begin(), it);
        if (!isWordChar(ch)) {
            break;
        }
        cursor -= 1;
    }
    // If we stopped because we hit a separator, move right to word start.
    if (cursor < length()) {
        it = utf8::nextNChar(text.begin(), cursor);
        uint32_t nextCh = 0;
        auto nextIt = utf8::getNextChar(it, text.end(), &nextCh);
        FCITX_UNUSED(nextIt);
        if (!isWordChar(nextCh) && cursor < length()) {
            // leave as is
        }
    }
}

void UnikeyState::InternalTextState::moveRight(bool byWord, bool extendSelection) {
    clampCursor();
    if (!extendSelection) {
        clearSelection();
    } else if (!selectionAnchor) {
        selectionAnchor = cursor;
    }

    const auto len = length();
    if (cursor >= len) {
        return;
    }

    if (!byWord) {
        cursor += 1;
        return;
    }

    // Word move: skip current word chars, then skip separators.
    while (cursor < len) {
        auto it = utf8::nextNChar(text.begin(), cursor);
        uint32_t ch = 0;
        utf8::getNextChar(it, text.end(), &ch);
        if (!isWordChar(ch)) {
            break;
        }
        cursor += 1;
    }
    while (cursor < len) {
        auto it = utf8::nextNChar(text.begin(), cursor);
        uint32_t ch = 0;
        utf8::getNextChar(it, text.end(), &ch);
        if (isWordChar(ch)) {
            break;
        }
        cursor += 1;
    }
}

void UnikeyState::InternalTextState::moveHome(bool extendSelection) {
    clampCursor();
    if (!extendSelection) {
        clearSelection();
    } else if (!selectionAnchor) {
        selectionAnchor = cursor;
    }
    cursor = 0;
}

void UnikeyState::InternalTextState::moveEnd(bool extendSelection) {
    clampCursor();
    if (!extendSelection) {
        clearSelection();
    } else if (!selectionAnchor) {
        selectionAnchor = cursor;
    }
    cursor = length();
}

UnikeyState::UnikeyState(UnikeyEngine *engine, InputContext *ic)
    : engine_(engine), uic_(engine->im()), ic_(ic) {}

void UnikeyState::keyEvent(KeyEvent &keyEvent) {
    // Ignore all key release.
    if (keyEvent.isRelease()) {
        // Do not clear lastShiftPressed_ here.
        //
        // Shift+Shift restoration is triggered by tapping two different shift
        // keys in sequence. In practice (and in our tests), a release event
        // for the first shift may be delivered before the second shift press.
        // If we clear the state on release, we lose the ability to detect the
        // tap sequence.
        return;
    }

    std::cerr << "[keyEvent] Key: " << keyEvent.rawKey().sym()
                         << " isRelease: " << keyEvent.isRelease()
                         << " Shift: " << keyEvent.rawKey().states().test(KeyState::Shift)
                         << " Ctrl: " << keyEvent.rawKey().states().test(KeyState::Ctrl)
                         << " Alt: " << keyEvent.rawKey().states().test(KeyState::Alt) << std::endl;

    // Snapshot whether immediate-commit is allowed for this keystroke.
    const bool allowImmediateCommitForThisKey = immediateCommitMode();

    if (keyEvent.key().isSimple()) {
        rebuildPreedit(keyEvent.rawKey().sym());
    }
    preedit(keyEvent, allowImmediateCommitForThisKey);

    // check last keyevent with shift
    if (keyEvent.rawKey().sym() >= FcitxKey_space &&
        keyEvent.rawKey().sym() <= FcitxKey_asciitilde) {
        lastKeyWithShift_ =
            keyEvent.rawKey().states().test(KeyState::Shift);
    } else {
        lastKeyWithShift_ = false;
    } // end check last keyevent with shift
}

bool UnikeyState::immediateCommitMode() const {
    if (!*this->engine_->config().immediateCommit) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Disabled in config";
        return false;
    }

    // This mode relies on *modifying* surrounding text (deleteSurroundingText),
    // but does not read surrounding text snapshots.
    if (*this->engine_->config().oc != UkConv::XUTF8) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Output charset is not XUTF8, is: "
                             << static_cast<int>(*this->engine_->config().oc);
        return false;
    }
    if (!ic_->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] SurroundingText capability not available (deleteSurroundingText unsupported)";
        return false;
    }
    FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] ENABLED";
    return true;
}

void UnikeyState::eraseChars(int num_chars) {
    int i;
    int k;
    unsigned char c;
    k = num_chars;

    for (i = preeditStr_.length() - 1; i >= 0 && k > 0; i--) {
        c = preeditStr_.at(i);

        // count down if byte is begin byte of utf-8 char
        if (c < (unsigned char)'\x80' || c >= (unsigned char)'\xC0') {
            k--;
        }
    }

    preeditStr_.erase(i + 1);
}

void UnikeyState::reset() {
    uic_.resetBuf();
    preeditStr_.clear();
    keyStrokes_.clear();
    updatePreedit();
    lastShiftPressed_ = FcitxKey_None;
}

void UnikeyState::clearImmediateCommitHistory() {
    // Legacy name: kept because UnikeyEngine already calls it on
    // InputContextReset (focus change). We now use it to clear only internal
    // state that must not be reset on every commit.
    clearInternalTextState();
}

void UnikeyState::clearInternalTextState() { internal_.clear(); }

void UnikeyState::deleteAroundCursor(int offset, int size) {
    // Apply to internal buffer first (best-effort), then send request.
    // offset/size are in code points.
    internal_.clampCursor();

    // Compute deletion range in [0..len].
    const auto len = internal_.length();
    int start = static_cast<int>(internal_.cursor) + offset;
    if (start < 0) {
        start = 0;
    }
    int end = start + size;
    if (end < 0) {
        end = 0;
    }
    if (start > static_cast<int>(len)) {
        start = static_cast<int>(len);
    }
    if (end > static_cast<int>(len)) {
        end = static_cast<int>(len);
    }
    if (end < start) {
        std::swap(start, end);
    }
    internal_.eraseRange(static_cast<size_t>(start), static_cast<size_t>(end));
    internal_.cursor = static_cast<size_t>(start);
    internal_.clearSelection();

    ic_->deleteSurroundingText(offset, size);
}

void UnikeyState::applyPassThroughKeyToInternalState(KeySym sym,
                                                    const KeyStates &state) {
    // We only model common editing/navigation keys.
    const bool shift = state.test(KeyState::Shift);
    const bool ctrl = state.test(KeyState::Ctrl);

    switch (sym) {
    case FcitxKey_Left:
    case FcitxKey_KP_Left:
        internal_.moveLeft(ctrl, shift);
        return;
    case FcitxKey_Right:
    case FcitxKey_KP_Right:
        internal_.moveRight(ctrl, shift);
        return;
    case FcitxKey_Home:
    case FcitxKey_KP_Home:
        internal_.moveHome(shift);
        return;
    case FcitxKey_End:
    case FcitxKey_KP_End:
        internal_.moveEnd(shift);
        return;
    case FcitxKey_BackSpace:
        internal_.backspace();
        return;
    case FcitxKey_Delete:
    case FcitxKey_KP_Delete:
        internal_.del();
        return;
    case FcitxKey_Return:
    case FcitxKey_KP_Enter:
        internal_.insertText("\n");
        internal_.clearSelection();
        return;
    case FcitxKey_Tab:
        internal_.insertText("\t");
        internal_.clearSelection();
        return;
    default:
        break;
    }

    // Ctrl+A: select all.
    if (ctrl && (sym == FcitxKey_a || sym == FcitxKey_A)) {
        internal_.selectionAnchor = 0;
        internal_.cursor = internal_.length();
        return;
    }
}

void UnikeyState::preedit(KeyEvent &keyEvent, bool allowImmediateCommitForThisKey) {
    auto sym = keyEvent.rawKey().sym();
    auto state = keyEvent.rawKey().states();

    std::cerr << "[preedit] Entering preedit with sym=" << sym << " Shift=" << state.test(KeyState::Shift) << std::endl;
    // for VNI input method (tone/shape keys are digits) and also matches user
    // expectations: KP_1 should behave like '1'.
    if (sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9) {
        sym = static_cast<KeySym>(FcitxKey_0 + (sym - FcitxKey_KP_0));
    }

    FCITX_INFO() << "[preedit] Processing key " << sym
                         << " Current preedit: \"" << preeditStr_ << "\"";

    // We try to detect Press and release of two different shift.
    // The sequence we want to detect is:
    if (keyEvent.rawKey().check(FcitxKey_Shift_L) ||
        keyEvent.rawKey().check(FcitxKey_Shift_R)) {
        // If we don't have any buffered keystrokes, there is nothing meaningful
        // to restore. Avoid arming the Shift+Shift sequence in that case.
        if (keyStrokes_.empty()) {
            lastShiftPressed_ = FcitxKey_None;
            return;
        }
        if (lastShiftPressed_ == FcitxKey_None) {
            lastShiftPressed_ = keyEvent.rawKey().sym();
        } else {
            // A second shift press (same or different) triggers restore.
            std::cerr << "[preedit] Shift+Shift detected, restoring keystrokes" << std::endl;
            uic_.restoreKeyStrokes();
            preeditStr_.clear();
            syncState(FcitxKey_None);
            updatePreedit();
            lastShiftPressed_ = FcitxKey_None;
            keyEvent.filterAndAccept();
            return;
        }
    } else {
        // We pressed something else, reset the state.
        lastShiftPressed_ = FcitxKey_None;
    }

    if (state.testAny(KeyState::Ctrl_Alt) || sym == FcitxKey_Control_L ||
        sym == FcitxKey_Control_R || sym == FcitxKey_Tab ||
        sym == FcitxKey_Return || sym == FcitxKey_Delete ||
        sym == FcitxKey_KP_Enter ||
        (sym >= FcitxKey_Home && sym <= FcitxKey_Insert) ||
        (sym >= FcitxKey_KP_Home && sym <= FcitxKey_KP_Delete)) {
        handleIgnoredKey(sym, state);
        return;
    }
    if (state.test(KeyState::Super)) {
        // Typically doesn't modify the text buffer.
        return;
    }
    if ((sym >= FcitxKey_Caps_Lock && sym <= FcitxKey_Hyper_R) ||
        sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R) {
        return;
    }
    if (sym == FcitxKey_BackSpace) {
        FCITX_INFO() << "[preedit] BackSpace pressed";
        if (immediateCommitMode()) {
            FCITX_INFO() << "[preedit] BackSpace in immediate commit mode";
            // If we have a tracked selection, delete it.
            if (internal_.hasSelection()) {
                const auto [s, e] = internal_.selectionRange();
                const auto len = e - s;
                if (len > 0) {
                    const int offset = static_cast<int>(s) - static_cast<int>(internal_.cursor);
                    deleteAroundCursor(offset, static_cast<int>(len));
                }
            } else {
                // Delete one character before cursor.
                deleteAroundCursor(-1, 1);
            }
            reset();
            keyEvent.filterAndAccept();
            return;
        }

        if (keyStrokes_.empty()) {
            // Nothing in preedit: let the application handle BackSpace,
            // but keep our internal buffer in sync.
            commit();
            applyPassThroughKeyToInternalState(sym, state);
            return;
        }

        auto currentLen = utf8::lengthValidated(preeditStr_);
        if (currentLen == utf8::INVALID_LENGTH) {
            currentLen = 0;
        }

        // Loop to pop keystrokes until the number of characters decreases.
        // This implements "delete whole character" behavior instead of
        // "progressive undo" (which might just remove a tone).
        do {
            keyStrokes_.pop_back();

            // If we started with nothing visible (or invalid), popping one key
            // is enough.
            if (currentLen == 0) {
                break;
            }

            // Simulate the new state to check length
            uic_.resetBuf();
            std::string tempStr;
            for (auto s : keyStrokes_) {
                uic_.filter(s);
                // Replicate syncState logic for string construction
                if (uic_.backspaces() > 0) {
                    int k = uic_.backspaces();
                    int i;
                    for (i = tempStr.length() - 1; i >= 0 && k > 0; i--) {
                        unsigned char c = tempStr.at(i);
                        if (c < 0x80 || c >= 0xC0) {
                            k--;
                        }
                    }
                    tempStr.erase(i + 1);
                }
                if (uic_.bufChars() > 0) {
                    if (*this->engine_->config().oc == UkConv::XUTF8) {
                        tempStr.append(
                            reinterpret_cast<const char *>(uic_.buf()),
                            uic_.bufChars());
                    } else {
                        unsigned char buf[CONVERT_BUF_SIZE + 1];
                        int bufSize = CONVERT_BUF_SIZE;
                        latinToUtf(buf, uic_.buf(), uic_.bufChars(), &bufSize);
                        tempStr.append((const char *)buf,
                                       CONVERT_BUF_SIZE - bufSize);
                    }
                } else if (s != FcitxKey_Shift_L && s != FcitxKey_Shift_R &&
                           s != FcitxKey_None) {
                    tempStr.append(utf8::UCS4ToUTF8(s));
                }
            }

            auto newLen = utf8::lengthValidated(tempStr);
            if (newLen == utf8::INVALID_LENGTH) {
                newLen = 0;
            }

            if (newLen < currentLen) {
                break;
            }

        } while (!keyStrokes_.empty());

        uic_.resetBuf();
        preeditStr_.clear();
        for (auto s : keyStrokes_) {
            uic_.filter(s);
            syncState(s);
        }

        if (preeditStr_.empty()) {
            commit();
            keyEvent.filterAndAccept();
            return;
        }

        updatePreedit();
        keyEvent.filterAndAccept();
        return;
    }
    if (sym >= FcitxKey_KP_Multiply && sym <= FcitxKey_KP_9) {
        handleIgnoredKey(sym, state);
        return;
    }
    if (sym >= FcitxKey_space && sym <= FcitxKey_asciitilde) {
        // capture ascii printable char
        uic_.setCapsState(state.test(KeyState::Shift),
                          state.test(KeyState::CapsLock));

        const bool immediateCommit = allowImmediateCommitForThisKey;

        // process sym

        // process sym

        // auto commit block removed to fix https://github.com/fcitx/fcitx5-unikey/issues/chep1
        // (prevents premature commit of initial consonants which breaks tone placement)


        if ((*this->engine_->config().im == UkTelex ||
             *this->engine_->config().im == UkSimpleTelex2) &&
            !*this->engine_->config().process_w_at_begin &&
            uic_.isAtWordBeginning() &&
            (sym == FcitxKey_w || sym == FcitxKey_W)) {
            if (immediateCommit) {
                FCITX_UNIKEY_DEBUG() << "[preedit] W at word beginning in immediate commit mode";
                uic_.putChar(sym);
                syncState(sym);
                commit();
                keyEvent.filterAndAccept();
                return;
            }
            FCITX_UNIKEY_DEBUG() << "[preedit] W at word beginning (normal mode)";
            uic_.putChar(sym);

            // Even when we are not "processing" W at the beginning of a word,
            // we should still keep it inside the IM's composition (preedit)
            // instead of letting it pass through to the application.
            // Mixing pass-through keys with preedit-managed keys would cause
            // inconsistent commits and breaks our tests' model.
            keyStrokes_.push_back(sym);
            syncState(sym);
            updatePreedit();
            keyEvent.filterAndAccept();
            return;
        }

        autoCommit_ = false;

        // shift + space, shift + shift event
        if (!lastKeyWithShift_ && state.test(KeyState::Shift) &&
            sym == FcitxKey_space && !uic_.isAtWordBeginning()) {
            std::cerr << "[preedit] Shift+Space detected, restoring keystrokes" << std::endl;
            uic_.restoreKeyStrokes();
            preeditStr_.clear();
            syncState(FcitxKey_None);
            preeditStr_.append(" ");
            commit();
            keyEvent.filterAndAccept();
            return;
        } else {
            uic_.filter(sym);
            keyStrokes_.push_back(sym);
        }
        // end shift + space
        // end process sym

        syncState(sym);

        if (immediateCommit) {
            FCITX_UNIKEY_DEBUG() << "[preedit] ImmediateCommit: committing \"" << preeditStr_ << "\"";
            commit();
            keyEvent.filterAndAccept();
            return;
        }

        // commit string: if need
        if (!preeditStr_.empty()) {
            if (preeditStr_.back() == sym && isWordBreakSym(sym)) {
                FCITX_UNIKEY_DEBUG() << "[preedit] Word break symbol detected, committing \"" << preeditStr_ << "\"";
                commit();
                keyEvent.filterAndAccept();
                return;
            }
        }
        // end commit string

        updatePreedit();
        keyEvent.filterAndAccept();
        return;
    } // end capture printable char

    // non process key
    handleIgnoredKey(sym, state);
}

void UnikeyState::handleIgnoredKey(KeySym sym, const KeyStates &state) {
    uic_.filter(0);
    syncState();
    commit();

    // Key is not handled by Unikey; it will reach the application.
    // Update our internal model accordingly.
    applyPassThroughKeyToInternalState(sym, state);
}

void UnikeyState::commit() {
    if (!preeditStr_.empty()) {
        std::cerr << "[commit] Committing string: \"" << preeditStr_ << "\"" << std::endl;
        internal_.insertText(preeditStr_);
        ic_->commitString(preeditStr_);
    } else {
        std::cerr << "[commit] Preedit is empty, nothing to commit" << std::endl;
    }
    reset();
}

void UnikeyState::syncState(KeySym sym) {
    // process result of ukengine
    std::cerr << "[syncState] Engine backspaces: " << uic_.backspaces()
                         << " bufChars: " << uic_.bufChars()
                         << " keySymbol: " << sym << std::endl;

    if (uic_.backspaces() > 0) {
        std::cerr << "[syncState] Backspaces requested: " << uic_.backspaces() << std::endl;
        if (static_cast<int>(preeditStr_.length()) <= uic_.backspaces()) {
            std::cerr << "[syncState] Clearing entire preedit" << std::endl;
            preeditStr_.clear();
        } else {
            eraseChars(uic_.backspaces());
        }
    }

    if (uic_.bufChars() > 0) {
        std::cerr << "[syncState] Engine output: " << uic_.bufChars() << " chars" << std::endl;
        if (*this->engine_->config().oc == UkConv::XUTF8) {
            preeditStr_.append(reinterpret_cast<const char *>(uic_.buf()),
                               uic_.bufChars());
        } else {
            unsigned char buf[CONVERT_BUF_SIZE + 1];
            int bufSize = CONVERT_BUF_SIZE;

            latinToUtf(buf, uic_.buf(), uic_.bufChars(), &bufSize);
            preeditStr_.append((const char *)buf, CONVERT_BUF_SIZE - bufSize);
        }
        std::cerr << "[syncState] After append: \"" << preeditStr_ << "\"" << std::endl;
    } else if (sym != FcitxKey_Shift_L && sym != FcitxKey_Shift_R &&
               sym != FcitxKey_None) // if ukengine not process
    {
        std::cerr << "[syncState] Engine didn't process, appending raw symbol: " << sym << std::endl;
        preeditStr_.append(utf8::UCS4ToUTF8(sym));
        std::cerr << "[syncState] After raw append: \"" << preeditStr_ << "\"" << std::endl;
    }
    // end process result of ukengine
}

void UnikeyState::updatePreedit() {
    auto &inputPanel = ic_->inputPanel();

    inputPanel.reset();

    if (!preeditStr_.empty()) {
        const auto useClientPreedit =
            ic_->capabilityFlags().test(CapabilityFlag::Preedit);
        Text preedit(preeditStr_,
                     useClientPreedit && *this->engine_->config().displayUnderline
                         ? TextFormatFlag::Underline
                         : TextFormatFlag::NoFlag);
        preedit.setCursor(preeditStr_.size());
        if (useClientPreedit) {
            inputPanel.setClientPreedit(preedit);
        } else {
            inputPanel.setPreedit(preedit);
        }
    }
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

} // namespace fcitx
