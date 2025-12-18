/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#ifndef _FCITX5_UNIKEY_UNIKEY_STATE_H_
#define _FCITX5_UNIKEY_UNIKEY_STATE_H_

#include <fcitx/inputcontextproperty.h>
#include <fcitx/event.h>
#include <fcitx-utils/keysym.h>
#include "unikeyinputcontext.h"
#include <string>

namespace fcitx {

class UnikeyEngine;
class InputContext;

class UnikeyState final : public InputContextProperty {
public:
    UnikeyState(UnikeyEngine *engine, InputContext *ic);
    ~UnikeyState() = default;

    void keyEvent(KeyEvent &keyEvent);
    void clearImmediateCommitHistory();
    void preedit(KeyEvent &keyEvent, bool allowImmediateCommitForThisKey);
    void handleIgnoredKey();
    void commit();
    void syncState(KeySym sym = FcitxKey_None);
    void updatePreedit();

    bool immediateCommitMode() const;
    bool isUnsupportedSurroundingApp() const;
    void eraseChars(int num_chars);
    void reset();

    void rebuildFromSurroundingText();
    size_t rebuildStateFromSurrounding(bool deleteSurrounding);
    size_t rebuildStateFromLastImmediateWord(bool deleteSurrounding, KeySym upcomingSym);
    void rebuildPreedit(KeySym upcomingSym);

    bool mayRebuildStateFromSurroundingText_ = false;

    // Transient flag set by rebuildStateFromSurrounding(): true if the rebuild
    // failed because surrounding text appears stale/truncated compared to the
    // last immediate-commit word. Used to decide whether it's appropriate to
    // use the lastImmediateWord_ fallback.
    bool lastSurroundingRebuildWasStale_ = false;

    // If surrounding text from the application is unreliable (e.g. Firefox
    // temporarily returns stale/empty surrounding text after a commit), we
    // should stop using immediate-commit mode and fall back to regular
    // composition (preedit) to avoid corrupting text.
    bool surroundingTextUnreliable_ = false;

    // Count consecutive surrounding text rebuild failures before marking
    // as unreliable. This prevents a single fluke from permanently
    // disabling immediate commit mode.
    static constexpr int kSurroundingFailureThreshold = 2;
    int surroundingFailureCount_ = 0;

    // Count consecutive successful surrounding operations. Used to recover
    // from the unreliable state if the application starts providing
    // accurate surrounding text again.
    static constexpr int kSurroundingRecoveryThreshold = 3;
    int surroundingSuccessCount_ = 0;

private:
    UnikeyEngine *engine_;
    UnikeyInputContext uic_;
    InputContext *ic_;
    bool lastKeyWithShift_ = false;
    std::string preeditStr_;
    bool autoCommit_ = false;
    KeySym lastShiftPressed_ = FcitxKey_None;

    // Last committed word in immediate-commit mode (UTF-8) and its character
    // count (Unicode code points). Used as a safe fallback when surrounding
    // text is temporarily stale/empty.
    std::string lastImmediateWord_;
    size_t lastImmediateWordCharCount_ = 0;
    bool recordNextCommitAsImmediateWord_ = false;
};

} // namespace fcitx

#endif // _FCITX5_UNIKEY_UNIKEY_STATE_H_
