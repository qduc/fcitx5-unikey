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

namespace fcitx {

UnikeyState::UnikeyState(UnikeyEngine *engine, InputContext *ic)
    : engine_(engine), uic_(engine->im()), ic_(ic) {}

void UnikeyState::keyEvent(KeyEvent &keyEvent) {
    // Ignore all key release.
    if (keyEvent.isRelease()) {
        if (keyEvent.rawKey().check(FcitxKey_Shift_L) ||
            keyEvent.rawKey().check(FcitxKey_Shift_R)) {
            lastShiftPressed_ = FcitxKey_None;
        }
        return;
    }

    FCITX_UNIKEY_DEBUG() << "[keyEvent] Key: " << keyEvent.rawKey().sym()
                         << " Shift: " << keyEvent.rawKey().states().test(KeyState::Shift)
                         << " Ctrl: " << keyEvent.rawKey().states().test(KeyState::Ctrl)
                         << " Alt: " << keyEvent.rawKey().states().test(KeyState::Alt);

    if (keyEvent.key().isSimple()) {
        rebuildPreedit(keyEvent.rawKey().sym());
    }
    preedit(keyEvent);

    // check last keyevent with shift
    if (keyEvent.rawKey().sym() >= FcitxKey_space &&
        keyEvent.rawKey().sym() <= FcitxKey_asciitilde) {
        lastKeyWithShift_ =
            keyEvent.rawKey().states().test(KeyState::Shift);
    } else {
        lastKeyWithShift_ = false;
    } // end check last keyevent with shift
}

bool UnikeyState::isFirefox() const {
    return ic_->program() == "firefox" || ic_->program() == "org.mozilla.firefox" ||
           ic_->program() == "firefox-bin" || ic_->program() == "Firefox";
}

bool UnikeyState::immediateCommitMode() const {
    if (!*this->engine_->config().immediateCommit) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Disabled in config";
        return false;
    }

    if (isFirefox()) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Disabled for Firefox";
        return false;
    }

    if (surroundingTextUnreliable_) {
        FCITX_UNIKEY_DEBUG()
            << "[immediateCommitMode] Surrounding text marked unreliable; "
               "falling back to preedit";
        return false;
    }
    // This mode relies on reading and modifying surrounding text.
    if (*this->engine_->config().oc != UkConv::XUTF8) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Output charset is not XUTF8, is: "
                             << static_cast<int>(*this->engine_->config().oc);
        return false;
    }
    if (!ic_->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] SurroundingText capability not available";
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

    FCITX_UNIKEY_DEBUG() << "[eraseChars] Erasing " << num_chars
                         << " chars from preedit: \"" << preeditStr_ << "\"";

    for (i = preeditStr_.length() - 1; i >= 0 && k > 0; i--) {
        c = preeditStr_.at(i);

        // count down if byte is begin byte of utf-8 char
        if (c < (unsigned char)'\x80' || c >= (unsigned char)'\xC0') {
            k--;
        }
    }

    preeditStr_.erase(i + 1);
    FCITX_UNIKEY_DEBUG() << "[eraseChars] After erase: \"" << preeditStr_ << "\"";
}

void UnikeyState::reset() {
    FCITX_UNIKEY_DEBUG() << "[reset] Resetting state, clearing preedit";
    uic_.resetBuf();
    preeditStr_.clear();
    updatePreedit();
    lastShiftPressed_ = FcitxKey_None;

    // Do not clear surroundingTextUnreliable_ here: reset() may be triggered by
    // applications frequently (e.g., on every key). Keeping it here would cause
    // constant flapping. We reset it in clearImmediateCommitHistory() instead,
    // which is only called on InputContextReset (focus change).
}

void UnikeyState::clearImmediateCommitHistory() {
    // Clear history used only for immediate-commit surrounding rewrite.
    // This is intended for InputContextReset / focus changes where the
    // surrounding context is no longer related to the last committed word.
    lastImmediateWord_.clear();
    lastImmediateWordCharCount_ = 0;
    recordNextCommitAsImmediateWord_ = false;
    lastSurroundingRebuildWasStale_ = false;

    // On focus change, give the new context a fresh chance. The new
    // application (or even a different field in the same app) may
    // provide reliable surrounding text.
    surroundingTextUnreliable_ = false;
    surroundingFailureCount_ = 0;
    surroundingSuccessCount_ = 0;
}

void UnikeyState::preedit(KeyEvent &keyEvent) {
    auto sym = keyEvent.rawKey().sym();
    auto state = keyEvent.rawKey().states();

    FCITX_UNIKEY_DEBUG() << "[preedit] Processing key " << sym
                         << " Current preedit: \"" << preeditStr_ << "\"";

    // We try to detect Press and release of two different shift.
    // The sequence we want to detect is:
    if (keyEvent.rawKey().check(FcitxKey_Shift_L) ||
        keyEvent.rawKey().check(FcitxKey_Shift_R)) {
        if (lastShiftPressed_ == FcitxKey_None) {
            lastShiftPressed_ = keyEvent.rawKey().sym();
        } else if (lastShiftPressed_ != keyEvent.rawKey().sym()) {
            // Another shift is pressed, do restore Key.
            FCITX_UNIKEY_DEBUG() << "[preedit] Shift+Shift detected, restoring keystrokes";
            uic_.restoreKeyStrokes();
            syncState(keyEvent.rawKey().sym());
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
        FCITX_UNIKEY_DEBUG() << "[preedit] Handling ignored key";
        handleIgnoredKey();
        return;
    }
    if (state.test(KeyState::Super)) {
        FCITX_UNIKEY_DEBUG() << "[preedit] Super key pressed, ignoring";
        return;
    }
    if ((sym >= FcitxKey_Caps_Lock && sym <= FcitxKey_Hyper_R) ||
        sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R) {
        FCITX_UNIKEY_DEBUG() << "[preedit] Modifier key, ignoring";
        return;
    }
    if (sym == FcitxKey_BackSpace) {
        FCITX_UNIKEY_DEBUG() << "[preedit] BackSpace pressed";
        if (immediateCommitMode()) {
            FCITX_UNIKEY_DEBUG() << "[preedit] BackSpace in immediate commit mode";
            ic_->updateSurroundingText();
            if (ic_->surroundingText().isValid() &&
                !ic_->surroundingText().selectedText().empty()) {
                FCITX_UNIKEY_DEBUG() << "[preedit] Text selected, resetting";
                reset();
                return;
            }
            // Immediate-commit mode: just delete one character and reset state.
            FCITX_UNIKEY_DEBUG() << "[preedit] Deleting surrounding text (-1, 1)";
            ic_->deleteSurroundingText(-1, 1);

            // After explicit deletion, we should not attempt to rewrite using
            // the last immediate word.
            lastImmediateWord_.clear();
            lastImmediateWordCharCount_ = 0;

            reset();
            keyEvent.filterAndAccept();
            return;
        }

        // capture BackSpace
        uic_.backspacePress();

        FCITX_UNIKEY_DEBUG() << "[preedit] BackSpace processing: backspaces=" << uic_.backspaces()
                             << " preeditLen=" << preeditStr_.length();

        if (uic_.backspaces() == 0 || preeditStr_.empty()) {
            FCITX_UNIKEY_DEBUG() << "[preedit] No backspaces or empty preedit, committing";
            commit();
            return;
        }
        if (static_cast<int>(preeditStr_.length()) <= uic_.backspaces()) {
            FCITX_UNIKEY_DEBUG() << "[preedit] Clearing preedit";
            preeditStr_.clear();
            autoCommit_ = true;
        } else {
            eraseChars(uic_.backspaces());
        }

        // change tone position after press backspace
        if (uic_.bufChars() > 0) {
            FCITX_UNIKEY_DEBUG() << "[preedit] Engine output buffer has " << uic_.bufChars() << " chars";
            if (this->engine_->config().oc.value() == UkConv::XUTF8) {
                preeditStr_.append(reinterpret_cast<const char *>(uic_.buf()),
                                   uic_.bufChars());
            } else {
                unsigned char buf[CONVERT_BUF_SIZE];
                int bufSize = CONVERT_BUF_SIZE;

                latinToUtf(buf, uic_.buf(), uic_.bufChars(), &bufSize);
                preeditStr_.append((const char *)buf,
                                   CONVERT_BUF_SIZE - bufSize);
            }

            autoCommit_ = false;
        }
        FCITX_UNIKEY_DEBUG() << "[preedit] After BackSpace, preedit: \"" << preeditStr_ << "\"";
        updatePreedit();

        keyEvent.filterAndAccept();
        return;
    }
    if (sym >= FcitxKey_KP_Multiply && sym <= FcitxKey_KP_9) {
        handleIgnoredKey();
        return;
    }
    if (sym >= FcitxKey_space && sym <= FcitxKey_asciitilde) {
        // capture ascii printable char
        FCITX_UNIKEY_DEBUG() << "[preedit] Printable char: " << static_cast<char>(sym)
                             << " (code: " << sym << ")";
        uic_.setCapsState(state.test(KeyState::Shift),
                          state.test(KeyState::CapsLock));

        const bool immediateCommit = immediateCommitMode();
        FCITX_UNIKEY_DEBUG() << "[preedit] ImmediateCommit mode: " << immediateCommit;

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
            if (!*this->engine_->config().macro) {
                return;
            }
            preeditStr_.append(sym == FcitxKey_w ? "w" : "W");
            updatePreedit();
            keyEvent.filterAndAccept();
            return;
        }

        autoCommit_ = false;

        // shift + space, shift + shift event
        if (!lastKeyWithShift_ && state.test(KeyState::Shift) &&
            sym == FcitxKey_space && !uic_.isAtWordBeginning()) {
            FCITX_UNIKEY_DEBUG() << "[preedit] Shift+Space detected, restoring keystrokes";
            uic_.restoreKeyStrokes();
        } else {
            FCITX_UNIKEY_DEBUG() << "[preedit] Filtering char through unikey engine";
            uic_.filter(sym);
        }
        // end shift + space
        // end process sym

        syncState(sym);

        if (immediateCommit) {
            FCITX_UNIKEY_DEBUG() << "[preedit] ImmediateCommit: committing \"" << preeditStr_ << "\"";
            // Record this commit as the latest immediate-commit word if it
            // looks like a word (no spaces / breaks). This will be used as a
            // fallback rewrite source when surrounding text is stale/empty.
            recordNextCommitAsImmediateWord_ = true;
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

        FCITX_UNIKEY_DEBUG() << "[preedit] After processing, preedit: \"" << preeditStr_ << "\"";
        updatePreedit();
        keyEvent.filterAndAccept();
        return;
    } // end capture printable char

    // non process key
    handleIgnoredKey();
}

void UnikeyState::handleIgnoredKey() {
    FCITX_UNIKEY_DEBUG() << "[handleIgnoredKey] Processing ignored key";
    uic_.filter(0);
    syncState();

    // This is not an immediate-commit keystroke. Avoid using it as a rewrite
    // source.
    recordNextCommitAsImmediateWord_ = false;
    commit();
}

void UnikeyState::commit() {
    if (recordNextCommitAsImmediateWord_) {
        recordNextCommitAsImmediateWord_ = false;
        // Only keep a safe "word" as rewrite source.
        // - Must be valid UTF-8
        // - Must not contain word-break symbols (ASCII)
        auto charLen = utf8::lengthValidated(preeditStr_);
        bool ok = (charLen != utf8::INVALID_LENGTH);
        if (ok && !preeditStr_.empty()) {
            for (const auto &c : preeditStr_) {
                // Non-ASCII bytes are allowed (Vietnamese letters).
                if (static_cast<unsigned char>(c) < 0x80) {
                    if (isWordBreakSym(static_cast<unsigned char>(c))) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        if (ok && !preeditStr_.empty()) {
            lastImmediateWord_ = preeditStr_;
            lastImmediateWordCharCount_ = static_cast<size_t>(charLen);
            FCITX_UNIKEY_DEBUG()
                << "[commit] Recorded last immediate word: \"" << lastImmediateWord_
                << "\" chars=" << lastImmediateWordCharCount_;
        } else {
            lastImmediateWord_.clear();
            lastImmediateWordCharCount_ = 0;
            FCITX_UNIKEY_DEBUG()
                << "[commit] Not recording immediate word (not a safe word)";
        }
    }

    if (!preeditStr_.empty()) {
        FCITX_UNIKEY_DEBUG() << "[commit] Committing string: \"" << preeditStr_ << "\"";
        ic_->commitString(preeditStr_);
    } else {
        FCITX_UNIKEY_DEBUG() << "[commit] Preedit is empty, nothing to commit";
    }
    reset();
}

void UnikeyState::syncState(KeySym sym) {
    // process result of ukengine
    FCITX_UNIKEY_DEBUG() << "[syncState] Engine backspaces: " << uic_.backspaces()
                         << " bufChars: " << uic_.bufChars()
                         << " keySymbol: " << sym;

    if (uic_.backspaces() > 0) {
        FCITX_UNIKEY_DEBUG() << "[syncState] Backspaces requested: " << uic_.backspaces();
        if (static_cast<int>(preeditStr_.length()) <= uic_.backspaces()) {
            FCITX_UNIKEY_DEBUG() << "[syncState] Clearing entire preedit";
            preeditStr_.clear();
        } else {
            eraseChars(uic_.backspaces());
        }
    }

    if (uic_.bufChars() > 0) {
        FCITX_UNIKEY_DEBUG() << "[syncState] Engine output: " << uic_.bufChars() << " chars";
        if (*this->engine_->config().oc == UkConv::XUTF8) {
            preeditStr_.append(reinterpret_cast<const char *>(uic_.buf()),
                               uic_.bufChars());
        } else {
            unsigned char buf[CONVERT_BUF_SIZE + 1];
            int bufSize = CONVERT_BUF_SIZE;

            latinToUtf(buf, uic_.buf(), uic_.bufChars(), &bufSize);
            preeditStr_.append((const char *)buf, CONVERT_BUF_SIZE - bufSize);
        }
        FCITX_UNIKEY_DEBUG() << "[syncState] After append: \"" << preeditStr_ << "\"";
    } else if (sym != FcitxKey_Shift_L && sym != FcitxKey_Shift_R &&
               sym != FcitxKey_None) // if ukengine not process
    {
        FCITX_UNIKEY_DEBUG() << "[syncState] Engine didn't process, appending raw symbol: " << sym;
        preeditStr_.append(utf8::UCS4ToUTF8(sym));
        FCITX_UNIKEY_DEBUG() << "[syncState] After raw append: \"" << preeditStr_ << "\"";
    }
    // end process result of ukengine
}

void UnikeyState::updatePreedit() {
    FCITX_UNIKEY_DEBUG() << "[updatePreedit] Updating preedit: \"" << preeditStr_ << "\"";

    auto &inputPanel = ic_->inputPanel();

    inputPanel.reset();

    if (!preeditStr_.empty()) {
        const auto useClientPreedit =
            ic_->capabilityFlags().test(CapabilityFlag::Preedit);
        FCITX_UNIKEY_DEBUG() << "[updatePreedit] Using " << (useClientPreedit ? "client" : "server")
                             << " preedit";
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
    } else {
        FCITX_UNIKEY_DEBUG() << "[updatePreedit] Preedit is empty";
    }
    ic_->updatePreedit();
    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}

} // namespace fcitx
