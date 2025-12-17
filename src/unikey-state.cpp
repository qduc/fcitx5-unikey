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

    if (keyEvent.key().isSimple()) {
        rebuildPreedit();
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

bool UnikeyState::immediateCommitMode() const {
    if (!*this->engine_->config().immediateCommit) {
        return false;
    }
    // This mode relies on reading and modifying surrounding text.
    if (*this->engine_->config().oc != UkConv::XUTF8) {
        return false;
    }
    if (!ic_->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
        return false;
    }
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
    updatePreedit();
    lastShiftPressed_ = FcitxKey_None;
}

void UnikeyState::preedit(KeyEvent &keyEvent) {
    auto sym = keyEvent.rawKey().sym();
    auto state = keyEvent.rawKey().states();

    // We try to detect Press and release of two different shift.
    // The sequence we want to detect is:
    if (keyEvent.rawKey().check(FcitxKey_Shift_L) ||
        keyEvent.rawKey().check(FcitxKey_Shift_R)) {
        if (lastShiftPressed_ == FcitxKey_None) {
            lastShiftPressed_ = keyEvent.rawKey().sym();
        } else if (lastShiftPressed_ != keyEvent.rawKey().sym()) {
            // Another shift is pressed, do restore Key.
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
        handleIgnoredKey();
        return;
    }
    if (state.test(KeyState::Super)) {
        return;
    }
    if ((sym >= FcitxKey_Caps_Lock && sym <= FcitxKey_Hyper_R) ||
        sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R) {
        return;
    }
    if (sym == FcitxKey_BackSpace) {
        if (immediateCommitMode()) {
            ic_->updateSurroundingText();
            if (ic_->surroundingText().isValid() &&
                !ic_->surroundingText().selectedText().empty()) {
                reset();
                return;
            }
            // Immediate-commit mode: just delete one character and reset state.
            ic_->deleteSurroundingText(-1, 1);
            reset();
            keyEvent.filterAndAccept();
            return;
        }

        // capture BackSpace
        uic_.backspacePress();

        if (uic_.backspaces() == 0 || preeditStr_.empty()) {
            commit();
            return;
        }
        if (static_cast<int>(preeditStr_.length()) <= uic_.backspaces()) {
            preeditStr_.clear();
            autoCommit_ = true;
        } else {
            eraseChars(uic_.backspaces());
        }

        // change tone position after press backspace
        if (uic_.bufChars() > 0) {
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
        uic_.setCapsState(state.test(KeyState::Shift),
                          state.test(KeyState::CapsLock));

        const bool immediateCommit = immediateCommitMode();

        // process sym

        // auto commit word that never need to change later in preedit string
        // (like consonant - phu am) if macro enabled, then not auto commit.
        // Because macro may change any word
        if (!immediateCommit && !*this->engine_->config().macro &&
            (uic_.isAtWordBeginning() || autoCommit_)) {
            if (isWordAutoCommit(sym)) {
                uic_.putChar(sym);
                autoCommit_ = true;
                return;
            }
        } // end auto commit

        if ((*this->engine_->config().im == UkTelex ||
             *this->engine_->config().im == UkSimpleTelex2) &&
            !*this->engine_->config().process_w_at_begin &&
            uic_.isAtWordBeginning() &&
            (sym == FcitxKey_w || sym == FcitxKey_W)) {
            if (immediateCommit) {
                uic_.putChar(sym);
                syncState(sym);
                commit();
                keyEvent.filterAndAccept();
                return;
            }
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
            uic_.restoreKeyStrokes();
        } else {
            uic_.filter(sym);
        }
        // end shift + space
        // end process sym

        syncState(sym);

        if (immediateCommit) {
            commit();
            keyEvent.filterAndAccept();
            return;
        }

        // commit string: if need
        if (!preeditStr_.empty()) {
            if (preeditStr_.back() == sym && isWordBreakSym(sym)) {
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
    handleIgnoredKey();
}

void UnikeyState::handleIgnoredKey() {
    uic_.filter(0);
    syncState();
    commit();
}

void UnikeyState::commit() {
    if (!preeditStr_.empty()) {
        ic_->commitString(preeditStr_);
    }
    reset();
}

void UnikeyState::syncState(KeySym sym) {
    // process result of ukengine
    if (uic_.backspaces() > 0) {
        if (static_cast<int>(preeditStr_.length()) <= uic_.backspaces()) {
            preeditStr_.clear();
        } else {
            eraseChars(uic_.backspaces());
        }
    }

    if (uic_.bufChars() > 0) {
        if (*this->engine_->config().oc == UkConv::XUTF8) {
            preeditStr_.append(reinterpret_cast<const char *>(uic_.buf()),
                               uic_.bufChars());
        } else {
            unsigned char buf[CONVERT_BUF_SIZE + 1];
            int bufSize = CONVERT_BUF_SIZE;

            latinToUtf(buf, uic_.buf(), uic_.bufChars(), &bufSize);
            preeditStr_.append((const char *)buf, CONVERT_BUF_SIZE - bufSize);
        }
    } else if (sym != FcitxKey_Shift_L && sym != FcitxKey_Shift_R &&
               sym != FcitxKey_None) // if ukengine not process
    {
        preeditStr_.append(utf8::UCS4ToUTF8(sym));
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
