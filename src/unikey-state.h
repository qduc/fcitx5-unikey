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
#include <fcitx-utils/utf8.h>
#include <optional>
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
    void handleIgnoredKey(KeySym sym, const KeyStates &state);
    void commit();
    void syncState(KeySym sym = FcitxKey_None);
    void updatePreedit();

    bool immediateCommitMode() const;
    void eraseChars(int num_chars);
    void reset();
    void rebuildPreedit(KeySym upcomingSym);

    // Clears internal state that should only be reset on focus change.
    // Called from UnikeyEngine on InputContextReset.
    void clearInternalTextState();

private:
    struct InternalTextState {
        // UTF-8 text buffer and cursor position in Unicode code points.
        std::string text;
        size_t cursor = 0;
        std::optional<size_t> selectionAnchor; // code point index

        bool hasSelection() const {
            return selectionAnchor && *selectionAnchor != cursor;
        }

        std::pair<size_t, size_t> selectionRange() const {
            if (!hasSelection()) {
                return {cursor, cursor};
            }
            return {std::min(cursor, *selectionAnchor),
                    std::max(cursor, *selectionAnchor)};
        }

        void clearSelection() { selectionAnchor.reset(); }

        void clear() {
            text.clear();
            cursor = 0;
            selectionAnchor.reset();
        }

        // Clamp cursor to valid range (0..len).
        void clampCursor();

        // Delete a range [start, end) in code points.
        void eraseRange(size_t start, size_t end);

        // Insert UTF-8 at cursor (replaces selection if any).
        void insertText(const std::string &utf8);

        // Backspace/delete semantics (operate on selection if any).
        void backspace();
        void del();

        // Cursor movement.
        void moveLeft(bool byWord, bool extendSelection);
        void moveRight(bool byWord, bool extendSelection);
        void moveHome(bool extendSelection);
        void moveEnd(bool extendSelection);

        // Utility: length in code points (0 if invalid UTF-8).
        size_t length() const;
    };

    void applyPassThroughKeyToInternalState(KeySym sym, const KeyStates &state);

    // Delete text around cursor (like deleteSurroundingText) AND keep internal
    // state consistent.
    void deleteAroundCursor(int offset, int size);

    UnikeyEngine *engine_;
    UnikeyInputContext uic_;
    InputContext *ic_;
    bool lastKeyWithShift_ = false;
    std::string preeditStr_;
    std::vector<KeySym> keyStrokes_;
    bool autoCommit_ = false;
    KeySym lastShiftPressed_ = FcitxKey_None;

    InternalTextState internal_;
};

} // namespace fcitx

#endif // _FCITX5_UNIKEY_UNIKEY_STATE_H_
