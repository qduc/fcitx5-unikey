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
#include <fcitx/surroundingtext.h>
#include "unikeyinputcontext.h"
#include "vnlexi.h"
#include <string>
#include <vector>

namespace fcitx {

class UnikeyEngine;
class InputContext;

class UnikeyState final : public InputContextProperty {
public:
    struct ImmediateCommitKeyStroke {
        KeySym sym = FcitxKey_None;
        bool passThrough = false;
        bool rebuiltVnChar = false;
        VnLexiName vn = vnl_nonVnChar;
    };

    struct ImmediateCommitSnapshot {
        std::string visibleText;
        std::vector<ImmediateCommitKeyStroke> strokes;
    };

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
    bool isFirefox() const;
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
    std::vector<KeySym> keyStrokes_;
    bool autoCommit_ = false;
    KeySym lastShiftPressed_ = FcitxKey_None;

    // --- Experimental: mouse-driven cursor-move detection ---
    // Our running belief of where the document caret is. It is anchored from the
    // application's surrounding-text cursor at the top of each key event and
    // advanced by our own commits/deletes during that event. If, at the next key
    // event, the app-reported cursor differs from this by an amount our own edits
    // cannot explain, the caret most likely moved without a key event (e.g. a
    // mouse click). -1 means "not yet anchored".
    int expectedCursor_ = -1;
    // A backward cursor delta is ambiguous with surrounding-text lag (a stale
    // snapshot reports an older, smaller cursor). Only treat a backward gap
    // larger than this many characters as a real caret move; smaller gaps are
    // absorbed as possible lag. Forward jumps need no tolerance — lag cannot
    // produce a larger-than-expected cursor.
    static constexpr int kCursorMoveBackwardTolerance = 1;
    // Net characters our own commits/deletes added to the document during the
    // current key event (positive = inserted, negative = deleted). Used only for
    // logging so we can attribute a cursor delta to our edits vs. a real move.
    int pendingDocDelta_ = 0;
    void commitStringTracked(const std::string &str);
    void deleteSurroundingTextTracked(int offset, int size);

    bool restorePreeditToRawKeystrokesIfAvailable();
    bool restorePreeditToRawAvailableHistory();
    void clearImmediateCommitSession();
    bool restoreImmediateCommitSession();
    bool canRewriteImmediateCommit() const;
    bool hasActiveSelection() const;
    static bool hasActiveSelection(const SurroundingText &st);
    void commitImmediateDiff(const std::string &oldWord,
                             const std::string &newWord,
                             KeySym fallbackSym = FcitxKey_None);
    void updateImmediateCommitSessionFromPreedit(int forcePassThroughIndex = -1);
    void recordImmediateCommitSnapshot();
    bool restoreImmediateCommitSnapshot(const std::string &visibleText);
    bool restoreImmediateCommitStrokes(
        const std::vector<ImmediateCommitKeyStroke> &strokes,
        const std::string *expectedVisibleText = nullptr);
    bool deriveImmediateCommitSnapshotForVisibleText(
        const std::string &visibleText);
    bool hasImmediateCommitSession() const;
    void replayImmediateCommitKeyStroke(const ImmediateCommitKeyStroke &stroke);
    bool tryReeditImmediateFromSurrounding(KeySym sym);
    bool canRewriteImmediateCommitSelectionPrefix(
        const std::string &oldWord) const;
    void setRebuiltImmediateReplayStrokes(
        std::vector<ImmediateCommitKeyStroke> strokes,
        size_t keyStrokeCount);

    // DEFERRED-DECISION flag. True when the visible preedit is a re-converted
    // view of external raw-ASCII text pulled from surrounding text. Rebuild
    // replays raw codes through uic_.filter(), so external "ca1" is displayed
    // as "cá". If the next key is plain Space, restore and commit the original
    // raw word ("ca1 "); if the next key edits the word, clear this flag and
    // continue normal converted composition.
    // Set only by rebuildStateFromSurrounding(), not by the internal
    // lastImmediateWord_ fallback.
    bool rawAsciiRebuiltFromSurrounding_ = false;

    // Set by Shift+Shift restoration in immediate-commit mode. The visible
    // preedit now contains the raw keystrokes for the already-committed word,
    // so the next plain Space should rewrite that word to raw text before
    // inserting the blank.
    bool restoreRawOnNextCommit_ = false;

    // Last committed word in immediate-commit mode (UTF-8) and its character
    // count (Unicode code points). Used as a safe fallback when surrounding
    // text is temporarily stale/empty.
    std::string lastImmediateWord_;
    size_t lastImmediateWordCharCount_ = 0;
    bool recordNextCommitAsImmediateWord_ = false;

    // Firefox-specific: Cursor offset from END of word (0 = at end).
    // Used to track cursor position for arrow key navigation and backspace.
    // Example: "ăn" with offset 0 → cursor after "n"
    //          "ăn" with offset 1 → cursor after "ă"
    size_t firefoxCursorOffsetFromEnd_ = 0;

    // ImmediateCommit-owned current word. This lets ImmediateCommit rewrite
    // text from internal keystroke history instead of app-reported surrounding
    // text, which is unreliable in many applications.
    std::string immediateCommitWord_;
    size_t immediateCommitWordCharCount_ = 0;
    std::vector<ImmediateCommitKeyStroke> immediateCommitKeyStrokes_;
    std::vector<ImmediateCommitSnapshot> immediateCommitSnapshots_;
    std::vector<ImmediateCommitKeyStroke> rebuiltImmediateReplayStrokes_;
    size_t rebuiltImmediateReplayKeyStrokeCount_ = 0;
};

} // namespace fcitx

#endif // _FCITX5_UNIKEY_UNIKEY_STATE_H_
