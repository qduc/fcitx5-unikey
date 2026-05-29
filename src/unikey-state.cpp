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

    // Space is treated as a literal space: it should commit the current
    // composition and insert a blank, never pull the previous word back from
    // surrounding text to re-run Vietnamese conversion. Skip the rebuild for
    // space so it never re-feeds the prior word into the Unikey engine.
    //
    // In immediate-commit mode we never rebuild from surrounding text at all
    // (no prefix bootstrap, no cursor recovery, no prefix reconstruction):
    // the current word is owned entirely by internal keystroke history. Only
    // non-immediate modes consult the application snapshot via rebuildPreedit().
    if (!immediateCommitMode() && keyEvent.key().isSimple() &&
        keyEvent.rawKey().sym() != FcitxKey_space) {
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
    // Firefox is now supported via internal state tracking for immediate commit mode.
    // Treat various LibreOffice frontends as unsupported for surrounding-text
    // handling due to inconsistent surrounding snapshots.
    if (prog == "libreoffice" || prog == "LibreOffice" ||
        prog == "soffice" || prog == "soffice.bin" ||
        prog == "libreoffice-writer" || prog == "org.libreoffice.LibreOffice") {
        return true;
    }

    return false;
}

bool UnikeyState::isFirefox() const {
    const auto prog = ic_->program();
    return (prog == "firefox" || prog == "org.mozilla.firefox" ||
            prog == "firefox-bin" || prog == "Firefox");
}

bool UnikeyState::immediateCommitMode() const {
    if (!*this->engine_->config().immediateCommit) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Disabled in config";
        return false;
    }

    // ImmediateCommit owns the current word internally and does not depend on
    // app-reported surrounding text. It only needs UTF-8 output so internal
    // character-count based rewrites are well defined.
    if (*this->engine_->config().oc != UkConv::XUTF8) {
        FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] Output charset is not XUTF8, is: "
                             << static_cast<int>(*this->engine_->config().oc);
        return false;
    }
    FCITX_UNIKEY_DEBUG() << "[immediateCommitMode] ENABLED (internal session mode)";
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
    rawAsciiRebuiltFromSurrounding_ = false;
    updatePreedit();
    lastShiftPressed_ = FcitxKey_None;

    // Do not clear surroundingTextUnreliable_ here: reset() may be triggered by
    // applications frequently (e.g., on every key). Keeping it here would cause
    // constant flapping. We reset it in clearImmediateCommitHistory() instead,
    // which is only called on InputContextReset (focus change).
}

void UnikeyState::clearImmediateCommitHistory() {
    clearImmediateCommitSession();

    // Clear history used only for immediate-commit surrounding rewrite.
    // This is intended for InputContextReset / focus changes where the
    // surrounding context is no longer related to the last committed word.
    lastImmediateWord_.clear();
    lastImmediateWordCharCount_ = 0;
    recordNextCommitAsImmediateWord_ = false;
    lastSurroundingRebuildWasStale_ = false;
    firefoxCursorOffsetFromEnd_ = 0;

    // On focus change, give the new context a fresh chance. The new
    // application (or even a different field in the same app) may
    // provide reliable surrounding text.
    surroundingTextUnreliable_ = false;
    surroundingFailureCount_ = 0;
    surroundingSuccessCount_ = 0;
}

bool UnikeyState::restorePreeditToRawKeystrokesIfAvailable() {
    uic_.restoreKeyStrokes();
    if (uic_.bufChars() <= 0) {
        return false;
    }

    preeditStr_.clear();
    syncState(FcitxKey_None);
    return true;
}

void UnikeyState::clearImmediateCommitSession() {
    immediateCommitWord_.clear();
    immediateCommitWordCharCount_ = 0;
    immediateCommitKeyStrokes_.clear();
}

bool UnikeyState::hasImmediateCommitSession() const {
    return !immediateCommitKeyStrokes_.empty() || !immediateCommitWord_.empty();
}

bool UnikeyState::canRewriteImmediateCommit() const {
    return ic_->capabilityFlags().test(CapabilityFlag::SurroundingText) &&
           !isUnsupportedSurroundingApp() &&
           (!ic_->surroundingText().isValid() ||
            ic_->surroundingText().selectedText().empty());
}

void UnikeyState::replayImmediateCommitKeyStroke(const ImmediateCommitKeyStroke &stroke) {
    if (stroke.passThrough) {
        uic_.putChar(stroke.sym);
    } else {
        uic_.filter(stroke.sym);
    }
    keyStrokes_.push_back(stroke.sym);
    syncState(stroke.sym);
}

bool UnikeyState::restoreImmediateCommitSession() {
    if (!hasImmediateCommitSession()) {
        return false;
    }

    uic_.resetBuf();
    preeditStr_.clear();
    keyStrokes_.clear();

    for (const auto &stroke : immediateCommitKeyStrokes_) {
        replayImmediateCommitKeyStroke(stroke);
    }
    return true;
}

void UnikeyState::commitImmediateDiff(const std::string &oldWord,
                                      const std::string &newWord,
                                      KeySym fallbackSym) {
    if (oldWord.empty()) {
        if (!newWord.empty()) {
            ic_->commitString(newWord);
        }
        return;
    }

    if (!canRewriteImmediateCommit()) {
        if (newWord.size() >= oldWord.size() &&
            newWord.compare(0, oldWord.size(), oldWord) == 0) {
            const std::string suffix = newWord.substr(oldWord.size());
            if (!suffix.empty()) {
                ic_->commitString(suffix);
            }
            return;
        }

        if (fallbackSym != FcitxKey_None && fallbackSym != FcitxKey_Shift_L &&
            fallbackSym != FcitxKey_Shift_R) {
            ic_->commitString(utf8::UCS4ToUTF8(fallbackSym));
        }
        return;
    }

    const size_t oldLen = utf8::lengthValidated(oldWord);
    if (oldLen != utf8::INVALID_LENGTH && oldLen > 0) {
        ic_->deleteSurroundingText(-static_cast<int>(oldLen),
                                   static_cast<int>(oldLen));
    }

    if (!newWord.empty()) {
        ic_->commitString(newWord);
    }
}

void UnikeyState::updateImmediateCommitSessionFromPreedit(int forcePassThroughIndex) {
    const size_t charLen = utf8::lengthValidated(preeditStr_);
    if (preeditStr_.empty() || charLen == utf8::INVALID_LENGTH) {
        clearImmediateCommitSession();
        return;
    }

    immediateCommitWord_ = preeditStr_;
    immediateCommitWordCharCount_ = charLen;
    std::vector<ImmediateCommitKeyStroke> updatedStrokes;
    updatedStrokes.reserve(keyStrokes_.size());
    for (size_t i = 0; i < keyStrokes_.size(); ++i) {
        const auto sym = keyStrokes_[i];
        const bool preservePassThrough =
            static_cast<int>(i) == forcePassThroughIndex ||
            (i < immediateCommitKeyStrokes_.size() &&
             immediateCommitKeyStrokes_[i].sym == sym &&
             immediateCommitKeyStrokes_[i].passThrough);
        updatedStrokes.push_back({sym, preservePassThrough});
    }
    immediateCommitKeyStrokes_ = std::move(updatedStrokes);
}

/**
 * Processes a key event for Vietnamese input method composition.
 *
 * This function handles the core logic for transforming keystrokes into Vietnamese text
 * using various input methods (Telex, VNI, VIQR). It manages preedit state, handles
 * special key combinations for restoration, and decides when to commit text to the
 * input context.
 *
 * Key behaviors:
 * - Shift+Shift: Restores previous keystrokes to allow editing
 * - Shift+Space: Commits current composition with a space
 * - BackSpace: Handles deletion, with special logic in immediate commit mode
 * - Printable characters: Processes through the Unikey engine for Vietnamese transformation
 * - Word breaks: Commits when encountering spaces or punctuation
 * - Special handling for 'W' in Telex mode at word beginnings
 *
 * @param keyEvent The key event to process
 * @param allowImmediateCommitForThisKey Whether immediate commit mode is allowed for this keystroke.
 *        When true, commits each character immediately instead of maintaining preedit state.
 *        This is used for applications with limited preedit support.
 */
void UnikeyState::preedit(KeyEvent &keyEvent, bool allowImmediateCommitForThisKey) {
    auto sym = keyEvent.rawKey().sym();
    auto state = keyEvent.rawKey().states();

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
        // Any navigation, control key, or shortcut (like Ctrl+A) breaks the
        // immediate editing context. handleIgnoredKey() will clear the state.
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
        FCITX_INFO() << "[preedit] BackSpace pressed";
        if (immediateCommitMode()) {
            FCITX_INFO() << "[preedit] BackSpace in immediate commit mode";
            if (!canRewriteImmediateCommit()) {
                clearImmediateCommitSession();
                reset();
                return;
            }
            if (!hasImmediateCommitSession()) {
                reset();
                return;
            }

            const std::string oldWord = immediateCommitWord_;
            restoreImmediateCommitSession();
            auto currentLen = utf8::lengthValidated(preeditStr_);
            if (currentLen == utf8::INVALID_LENGTH) {
                currentLen = 0;
            }

            do {
                immediateCommitKeyStrokes_.pop_back();

                if (currentLen == 0) {
                    break;
                }

                uic_.resetBuf();
                std::string tempStr;
                for (const auto &stroke : immediateCommitKeyStrokes_) {
                    if (stroke.passThrough) {
                        uic_.putChar(stroke.sym);
                    } else {
                        uic_.filter(stroke.sym);
                    }
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
                        tempStr.append(reinterpret_cast<const char *>(uic_.buf()),
                                       uic_.bufChars());
                    } else if (stroke.sym != FcitxKey_Shift_L &&
                               stroke.sym != FcitxKey_Shift_R &&
                               stroke.sym != FcitxKey_None) {
                        tempStr.append(utf8::UCS4ToUTF8(stroke.sym));
                    }
                }

                auto newLen = utf8::lengthValidated(tempStr);
                if (newLen == utf8::INVALID_LENGTH) {
                    newLen = 0;
                }

                if (newLen < currentLen) {
                    break;
                }
            } while (!immediateCommitKeyStrokes_.empty());

            uic_.resetBuf();
            preeditStr_.clear();
            keyStrokes_.clear();
            for (const auto &stroke : immediateCommitKeyStrokes_) {
                replayImmediateCommitKeyStroke(stroke);
            }

            commitImmediateDiff(oldWord, preeditStr_);
            updateImmediateCommitSessionFromPreedit();

            reset();
            keyEvent.filterAndAccept();
            return;
        }

        if (keyStrokes_.empty()) {
            commit();
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
        handleIgnoredKey();
        return;
    }
    if (sym >= FcitxKey_space && sym <= FcitxKey_asciitilde) {
        // capture ascii printable char
        uic_.setCapsState(state.test(KeyState::Shift),
                          state.test(KeyState::CapsLock));

        const bool immediateCommit = allowImmediateCommitForThisKey;
        const std::string oldImmediateWord = immediateCommitWord_;
        if (immediateCommit) {
            restoreImmediateCommitSession();
        }

        if (rawAsciiRebuiltFromSurrounding_ && sym != FcitxKey_space) {
            // Any non-space printable key edits the word, so drop the deferred
            // raw-commit preference and continue normal converted composition.
            rawAsciiRebuiltFromSurrounding_ = false;
        }

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
                keyStrokes_.push_back(sym);
                syncState(sym);
                commitImmediateDiff(oldImmediateWord, preeditStr_, sym);
                updateImmediateCommitSessionFromPreedit(
                    static_cast<int>(keyStrokes_.size()) - 1);
                reset();
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

        if (sym == FcitxKey_space) {
            // Plain Space commits the current visible preedit without re-running
            // IM conversion. Restore the raw keystrokes first only for explicit
            // Shift+Space, or for the deferred raw-ASCII surrounding-text case
            // where replay displayed external raw input like "ca1" as converted
            // "cá".
            const bool restoreRaw =
                state.test(KeyState::Shift) ||
                (!immediateCommit && rawAsciiRebuiltFromSurrounding_);
            if (restoreRaw) {
                restorePreeditToRawKeystrokesIfAvailable();
            }
            rawAsciiRebuiltFromSurrounding_ = false;
            if (immediateCommit) {
                if (restoreRaw) {
                    commitImmediateDiff(oldImmediateWord, preeditStr_);
                }
                ic_->commitString(" ");
                clearImmediateCommitSession();
                reset();
                keyEvent.filterAndAccept();
                return;
            }
            preeditStr_.append(" ");
            commit();
            keyEvent.filterAndAccept();
            return;
        }

        // shift + shift event (two different shift keys, no space)
        uic_.filter(sym);
        keyStrokes_.push_back(sym);
        // end process sym

        syncState(sym);

        if (immediateCommit) {
            FCITX_UNIKEY_DEBUG() << "[preedit] ImmediateCommit: committing \"" << preeditStr_ << "\"";
            commitImmediateDiff(oldImmediateWord, preeditStr_, sym);
            updateImmediateCommitSessionFromPreedit();
            reset();
            keyEvent.filterAndAccept();
            return;
        }

        // commit string: if need
        if (!preeditStr_.empty()) {
            if (preeditStr_.back() == sym && isWordBreakSym(sym)) {
                FCITX_UNIKEY_DEBUG() << "[preedit] Word break symbol detected, committing \"" << preeditStr_ << "\"";
                // If we are in modifySurroundingText mode (even if immediateCommit is disabled),
                // we should record this word to help detect stale surrounding text (e.g. in Firefox).
                if (*this->engine_->config().modifySurroundingText) {
                    // recordNextCommitAsImmediateWord_ = true;
                }
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

    // This is not an immediate-commit keystroke. Avoid using it as a rewrite
    // source.
    recordNextCommitAsImmediateWord_ = false;

    // Since we are passing an ignored key to the application, the cursor context
    // or text content may change unpredictably. Clear immediate word tracking.
    clearImmediateCommitSession();
    lastImmediateWord_.clear();
    lastImmediateWordCharCount_ = 0;
    lastSurroundingRebuildWasStale_ = false;
    firefoxCursorOffsetFromEnd_ = 0;

    commit();
}

void UnikeyState::commit() {
    // For Firefox, always record commits to maintain internal state for forward typing.
    // For other apps, only record when explicitly requested.
    bool shouldRecord = recordNextCommitAsImmediateWord_;
    if (isFirefox() && immediateCommitMode()) {
        shouldRecord = true;
    }

    if (shouldRecord) {
        recordNextCommitAsImmediateWord_ = false;

        // Strip trailing word break symbols (e.g. space) to extract the actual word.
        // The surrounding text checking logic expects the "word" part to match.
        std::string candidate = preeditStr_;
        while (!candidate.empty()) {
            unsigned char last = static_cast<unsigned char>(candidate.back());
            if (last < 0x80 && isWordBreakSym(last)) {
                candidate.pop_back();
            } else {
                break;
            }
        }

        // Only keep a safe "word" as rewrite source.
        // - Must be valid UTF-8
        // - Must not contain word-break symbols (ASCII)
        auto charLen = utf8::lengthValidated(candidate);
        bool ok = (charLen != utf8::INVALID_LENGTH);
        if (ok && !candidate.empty()) {
            for (const auto &c : candidate) {
                // Non-ASCII bytes are allowed (Vietnamese letters).
                if (static_cast<unsigned char>(c) < 0x80) {
                    if (isWordBreakSym(static_cast<unsigned char>(c))) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        if (ok && !candidate.empty()) {
            lastImmediateWord_ = candidate;
            lastImmediateWordCharCount_ = static_cast<size_t>(charLen);
            firefoxCursorOffsetFromEnd_ = 0;  // Reset cursor to word end after commit
        } else {
            lastImmediateWord_.clear();
            lastImmediateWordCharCount_ = 0;
            firefoxCursorOffsetFromEnd_ = 0;
        }
    }

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
