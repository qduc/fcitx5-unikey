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

    // Snapshot whether immediate-commit is allowed for this keystroke BEFORE
    // any surrounding-text rebuild attempts. rebuildPreedit() may mark
    // surrounding text as unreliable, but we still want the current keystroke
    // (that triggered the threshold) to behave consistently.
    bool allowImmediateCommitForThisKey = immediateCommitMode();

    // Special-case: when surrounding text has been marked unreliable, we
    // generally fall back to preedit for safety. However, for VNI tone/shape
    // keys (digits) we can still safely rewrite using our internal
    // lastImmediateWord_ history, without relying on the application's
    // surrounding snapshot.
    if (!allowImmediateCommitForThisKey && surroundingTextUnreliable_ &&
        *this->engine_->config().immediateCommit &&
        (*this->engine_->config().im == UkVni) &&
        !lastImmediateWord_.empty()) {
        const auto sym = keyEvent.rawKey().sym();
        const bool isDigit =
            (sym >= FcitxKey_0 && sym <= FcitxKey_9) ||
            (sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9);
        if (isDigit) {
            allowImmediateCommitForThisKey = true;
        }
    }

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

bool UnikeyState::isUnsupportedSurroundingApp() const {
    const auto prog = ic_->program();
    // Existing Firefox matches
    if (prog == "firefox" || prog == "org.mozilla.firefox" ||
        prog == "firefox-bin" || prog == "Firefox") {
        return true;
    }
    // Treat various LibreOffice frontends as unsupported for surrounding-text
    // handling (similar to Firefox) due to inconsistent surrounding snapshots.
    if (prog == "libreoffice" || prog == "LibreOffice" ||
        prog == "soffice" || prog == "soffice.bin" ||
        prog == "libreoffice-writer" || prog == "org.libreoffice.LibreOffice") {
        return true;
    }

    return false;
}

bool UnikeyState::immediateCommitMode() const {
    if (!*this->engine_->config().immediateCommit) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Disabled in config";
        return false;
    }

    if (isUnsupportedSurroundingApp()) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Disabled for unsupported app (Firefox/LibreOffice)";
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
    keyStrokes_.clear();
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
            FCITX_UNIKEY_DEBUG() << "[preedit] Shift key with empty keystrokes, ignoring";
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
        FCITX_INFO() << "[preedit] BackSpace pressed";
        if (immediateCommitMode()) {
            FCITX_INFO() << "[preedit] BackSpace in immediate commit mode";
            ic_->updateSurroundingText();
            if (ic_->surroundingText().isValid() &&
                !ic_->surroundingText().selectedText().empty()) {
                FCITX_INFO() << "[preedit] Text selected, resetting";
                reset();
                return;
            }
            // Immediate-commit mode: just delete one character and reset state.
            FCITX_INFO() << "[preedit] Deleting surrounding text (-1, 1)";
            ic_->deleteSurroundingText(-1, 1);

            // After explicit deletion, we should not attempt to rewrite using
            // the last immediate word.
            lastImmediateWord_.clear();
            lastImmediateWordCharCount_ = 0;

            reset();
            keyEvent.filterAndAccept();
            return;
        }

        if (keyStrokes_.empty()) {
            commit();
            return;
        }

        keyStrokes_.pop_back();
        uic_.resetBuf();
        preeditStr_.clear();
        for (auto s : keyStrokes_) {
            uic_.filter(s);
            syncState(s);
        }

        if (preeditStr_.empty()) {
            commit();
            return;
        }

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

        const bool immediateCommit = allowImmediateCommitForThisKey;
        FCITX_UNIKEY_DEBUG() << "[preedit] ImmediateCommit mode (snapshotted): "
                     << immediateCommit;

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
            FCITX_UNIKEY_DEBUG() << "[preedit] Filtering char through unikey engine";
            uic_.filter(sym);
            keyStrokes_.push_back(sym);
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
            std::cerr << "[commit] Recorded last immediate word: \"" << lastImmediateWord_
                << "\" chars=" << lastImmediateWordCharCount_ << std::endl;
        } else {
            lastImmediateWord_.clear();
            lastImmediateWordCharCount_ = 0;
            std::cerr << "[commit] Not recording immediate word (not a safe word)" << std::endl;
        }
    }

    if (!preeditStr_.empty()) {
        std::cerr << "[commit] Committing string: \"" << preeditStr_ << "\"" << std::endl;
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
