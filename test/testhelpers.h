/*
 * SPDX-FileCopyrightText: 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Shared test helpers for data-driven testing.
 *
 * Provides a TestEnv struct that wraps common test operations with short
 * names, eliminating repetitive testfrontend->call<ITestFrontend::...>(...)
 * boilerplate across all unit test files.
 */

#ifndef FCITX5_UNIKEY_TESTHELPERS_H
#define FCITX5_UNIKEY_TESTHELPERS_H

#include "testfrontend_public.h"
#include <fcitx-utils/key.h>
#include <fcitx/addoninstance.h>
#include <fcitx/inputcontext.h>
#include <string>

namespace fcitx {

/// Wraps a test frontend + input context with short-named helpers for
/// common test operations (key events, commit expectations, etc.).
struct TestEnv {
    AddonInstance *frontend;
    ICUUID uuid;
    InputContext *ic;

    /// Send a single key event (press, not release).
    void type(const std::string &key) {
        frontend->call<ITestFrontend::keyEvent>(uuid, Key(key), false);
    }

    /// Send a key event by keysym (for non-character keys like BackSpace).
    void type(KeySym sym) {
        frontend->call<ITestFrontend::keyEvent>(uuid, Key(sym), false);
    }

    /// Push a commit expectation: the next key event must produce this text.
    void expect(const std::string &commit) {
        frontend->call<ITestFrontend::pushCommitExpectation>(commit);
    }

    /// Combine expect + type: push a commit expectation, then type a key.
    void expectThenType(const std::string &commit, const std::string &key) {
        expect(commit);
        type(key);
    }

    /// Type each character of a multi-character string as individual key
    /// events (useful for data-driven map tests).
    void typeChars(const std::string &s) {
        for (char c : s) {
            type(std::string(1, c));
        }
    }

    /// Type each character, expecting each to commit immediately (common in
    /// immediate-commit mode where every keystroke produces a commit).
    void typeEachExpect(const std::string &s) {
        for (char c : s) {
            expectThenType(std::string(1, c), std::string(1, c));
        }
    }

    /// Reset the input context with empty surrounding text.
    void resetIC() {
        ic->reset();
        ic->surroundingText().setText("", 0, 0);
        ic->updateSurroundingText();
    }

    /// Set surrounding text. When sel is -1 (default), it uses cursor as
    /// both cursor and selection anchor (no selection).
    void setSurrounding(const std::string &text, int cursor, int sel = -1) {
        ic->surroundingText().setText(text, cursor, sel >= 0 ? sel : cursor);
        ic->updateSurroundingText();
    }
};

} // namespace fcitx

#endif // FCITX5_UNIKEY_TESTHELPERS_H
