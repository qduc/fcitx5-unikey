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
    void preedit(KeyEvent &keyEvent);
    void handleIgnoredKey();
    void commit();
    void syncState(KeySym sym = FcitxKey_None);
    void updatePreedit();

    bool immediateCommitMode() const;
    void eraseChars(int num_chars);
    void reset();

    void rebuildFromSurroundingText();
    size_t rebuildStateFromSurrounding(bool deleteSurrounding);
    void rebuildPreedit();

    bool mayRebuildStateFromSurroundingText_ = false;

private:
    UnikeyEngine *engine_;
    UnikeyInputContext uic_;
    InputContext *ic_;
    bool lastKeyWithShift_ = false;
    std::string preeditStr_;
    bool autoCommit_ = false;
    KeySym lastShiftPressed_ = FcitxKey_None;
};

} // namespace fcitx

#endif // _FCITX5_UNIKEY_UNIKEY_STATE_H_
