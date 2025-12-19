/*
 * SPDX-FileCopyrightText: 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests for internal state tracking (InternalTextState):
 * - Cursor movement (arrows, Home/End, word jumping)
 * - Selection creation and manipulation (Shift+arrows)
 * - Delete key behavior
 * - Vietnamese word boundary navigation
 * - Selection replacement with typed text
 * - UTF-8 edge cases (multi-byte characters)
 * - Return/Tab handling
 * - State consistency across operations
 */

#include "testdir.h"
#include "testfrontend_public.h"
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>

#include <iostream>

using namespace fcitx;

namespace {

struct CaseSelection {
    // 0 means run all cases.
    int caseId = 0;
    bool listCases = false;
};

bool shouldRunCase(const CaseSelection &sel, int id) {
    return sel.caseId == 0 || sel.caseId == id;
}

void printCases() {
    std::cout << "Available cases for testinternalstate:\n";
    std::cout << "  1: Basic cursor movement - Left arrow\n";
    std::cout << "  2: Basic cursor movement - Right arrow\n";
    std::cout << "  3: Basic cursor movement - Home key\n";
    std::cout << "  4: Basic cursor movement - End key\n";
    std::cout << "  5: Cursor movement at boundaries (Left at position 0)\n";
    std::cout << "  6: Cursor movement at boundaries (Right at end)\n";
    std::cout << "  7: Word jumping - Ctrl+Left over ASCII\n";
    std::cout << "  8: Word jumping - Ctrl+Right over ASCII\n";
    std::cout << "  9: Word jumping - Ctrl+Left over Vietnamese\n";
    std::cout << " 10: Word jumping - Ctrl+Right over Vietnamese\n";
    std::cout << " 11: Word jumping - Mixed ASCII and Vietnamese\n";
    std::cout << " 12: Selection with Shift+Right\n";
    std::cout << " 13: Selection with Shift+Left\n";
    std::cout << " 14: Selection with Shift+End\n";
    std::cout << " 15: Selection with Shift+Home\n";
    std::cout << " 16: Word selection with Shift+Ctrl+Right\n";
    std::cout << " 17: Word selection with Shift+Ctrl+Left\n";
    std::cout << " 18: Selection expansion and contraction\n";
    std::cout << " 19: Forward vs backward selection\n";
    std::cout << " 20: Delete key with no selection\n";
    std::cout << " 21: Delete key with selection\n";
    std::cout << " 22: Delete at text boundaries\n";
    std::cout << " 23: Selection replacement with typed text\n";
    std::cout << " 24: Multi-byte UTF-8 character handling\n";
    std::cout << " 25: Vietnamese tone-marked characters in cursor movement\n";
    std::cout << " 26: Return key handling in immediate commit mode\n";
    std::cout << " 27: Tab key handling in immediate commit mode\n";
    std::cout << " 28: State consistency after Vietnamese composition\n";
    std::cout << " 29: State consistency after tone changes\n";
    std::cout << " 30: Complex sequence - type, select, delete, type again\n";
    std::cout << " 31: Ctrl+BackSpace word deletion\n";
    std::cout << " 32: Ctrl+Delete word deletion\n";
}

void announceCase(int id) {
    // Print unconditionally to make it easy for tooling (ctest wrappers) to
    // detect the failing case even when fcitx logging sinks are not active.
    std::cerr << "testinternalstate: Case " << id << std::endl;
}

void setupInputMethodGroup(Instance *instance) {
    auto defaultGroup = instance->inputMethodManager().currentGroup();
    defaultGroup.inputMethodList().clear();
    defaultGroup.inputMethodList().push_back(InputMethodGroupItem("keyboard-us"));
    defaultGroup.inputMethodList().push_back(InputMethodGroupItem("unikey"));
    defaultGroup.setDefaultInputMethod("");
    instance->inputMethodManager().setGroup(defaultGroup);
}

void scheduleEvent(EventDispatcher *dispatcher, Instance *instance,
                   const CaseSelection &sel) {
    const auto selCopy = sel;
    dispatcher->schedule([dispatcher, instance, selCopy]() {
        auto *unikey = instance->addonManager().addon("unikey", true);
        FCITX_ASSERT(unikey);

        setupInputMethodGroup(instance);

        auto *testfrontend = instance->addonManager().addon("testfrontend");
        FCITX_ASSERT(testfrontend);

        auto uuid = testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(ic);
        ic->setCapabilityFlags(CapabilityFlag::SurroundingText);

        // Switch to Unikey.
        testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+space"), false);

        // Base config for all tests (immediate commit mode for internal state tracking)
        RawConfig config;
        config.setValueByPath("ImmediateCommit", "True");
        config.setValueByPath("InputMethod", "Telex");
        config.setValueByPath("SpellCheck", "False");
        config.setValueByPath("Macro", "False");
        config.setValueByPath("AutoNonVnRestore", "False");
        config.setValueByPath("OutputCharset", "Unicode");
        unikey->setConfig(config);

        // ==========================================================
        // BASIC CURSOR MOVEMENT TESTS
        // ==========================================================

        // --- Test: Basic cursor movement - Left arrow ---
        if (shouldRunCase(selCopy, 1)) {
            announceCase(1);
            FCITX_INFO() << "testinternalstate: Case 1 - Basic cursor movement - Left arrow";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Move left once (cursor: abc|  → ab|c)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);

            // Type 'k', should insert before 'c' → "abkc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abkc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Basic cursor movement - Right arrow ---
        if (shouldRunCase(selCopy, 2)) {
            announceCase(2);
            FCITX_INFO() << "testinternalstate: Case 2 - Basic cursor movement - Right arrow";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Move left twice then right once (abc| → a|bc → ab|c)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Right"), false);

            // Type 'k' → "abkc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abkc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Basic cursor movement - Home key ---
        if (shouldRunCase(selCopy, 3)) {
            announceCase(3);
            FCITX_INFO() << "testinternalstate: Case 3 - Basic cursor movement - Home key";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Press Home (cursor: abc| → |abc)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);

            // Type 'k' → "kabc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("kabc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Basic cursor movement - End key ---
        if (shouldRunCase(selCopy, 4)) {
            announceCase(4);
            FCITX_INFO() << "testinternalstate: Case 4 - Basic cursor movement - End key";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Press Home then End (|abc → abc|)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("End"), false);

            // Type 'k' → "abck"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abck");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Cursor movement at boundaries (Left at position 0) ---
        if (shouldRunCase(selCopy, 5)) {
            announceCase(5);
            FCITX_INFO() << "testinternalstate: Case 5 - Cursor movement at boundaries (Left at position 0)";

            ic->reset();

            // Type "a"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Press Home to move to start
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);

            // Press Left again (should do nothing, cursor stays at 0)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);

            // Type 'k' → "ka"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ka");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Cursor movement at boundaries (Right at end) ---
        if (shouldRunCase(selCopy, 6)) {
            announceCase(6);
            FCITX_INFO() << "testinternalstate: Case 6 - Cursor movement at boundaries (Right at end)";

            ic->reset();

            // Type "a"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Press Right (should do nothing, cursor stays at end)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Right"), false);

            // Type 'k' → "ak"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ak");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // ==========================================================
        // WORD JUMPING TESTS
        // ==========================================================

        // --- Test: Word jumping - Ctrl+Left over ASCII ---
        if (shouldRunCase(selCopy, 7)) {
            announceCase(7);
            FCITX_INFO() << "testinternalstate: Case 7 - Word jumping - Ctrl+Left over ASCII";

            ic->reset();

            // Type "hello planet" (avoid Telex special letters like 'w')
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("p");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pl");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pla");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plan");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plane");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Ctrl+Left (should jump to start of "planet")
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+Left"), false);

            // Type 'k' → "kplanet"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("kplanet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Word jumping - Ctrl+Right over ASCII ---
        if (shouldRunCase(selCopy, 8)) {
            announceCase(8);
            FCITX_INFO() << "testinternalstate: Case 8 - Word jumping - Ctrl+Right over ASCII";

            ic->reset();

            // Type "hello planet" (avoid Telex special letters like 'w')
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("p");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pl");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pla");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plan");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plane");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Move to start then Ctrl+Right (should jump to end of "hello")
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+Right"), false);

            // Type 'k' at the new cursor position → "hellok planet"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hellok");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Word jumping - Ctrl+Left over Vietnamese ---
        if (shouldRunCase(selCopy, 9)) {
            announceCase(9);
            FCITX_INFO() << "testinternalstate: Case 9 - Word jumping - Ctrl+Left over Vietnamese";

            ic->reset();

            // Type "xin chào" (hello in Vietnamese)
            // "xin"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);

            // "chào" (using Telex: chaf, ow, f)
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin ch");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin cha");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin chà");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin chào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Ctrl+Left (should jump to start of "chào")
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+Left"), false);

            // Type 'k' → "xin kchào"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin kchào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Word jumping - Ctrl+Right over Vietnamese ---
        if (shouldRunCase(selCopy, 10)) {
            announceCase(10);
            FCITX_INFO() << "testinternalstate: Case 10 - Word jumping - Ctrl+Right over Vietnamese";

            ic->reset();

            // Type "xin chào"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin ch");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin cha");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin chà");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin chào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Move to start then Ctrl+Right (should jump to end of "xin")
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+Right"), false);

            // Type 'k' → "xink chào"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xink chào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Word jumping - Mixed ASCII and Vietnamese ---
        if (shouldRunCase(selCopy, 11)) {
            announceCase(11);
            FCITX_INFO() << "testinternalstate: Case 11 - Word jumping - Mixed ASCII and Vietnamese";

            ic->reset();

            // Type "hello chào"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ch");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("cha");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("chà");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("chào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Ctrl+Left twice (should jump: chào| → hello | → |hello)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+Left"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+Left"), false);

            // Type 'k' → "khello chào"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("khello chào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // ==========================================================
        // SELECTION TESTS
        // ==========================================================

        // --- Test: Selection with Shift+Right ---
        if (shouldRunCase(selCopy, 12)) {
            announceCase(12);
            FCITX_INFO() << "testinternalstate: Case 12 - Selection with Shift+Right";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Move to start
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);

            // Select 2 characters with Shift+Right twice
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Right"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Right"), false);

            // Type 'k' to replace selection
            testfrontend->call<ITestFrontend::pushCommitExpectation>("kc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Selection with Shift+Left ---
        if (shouldRunCase(selCopy, 13)) {
            announceCase(13);
            FCITX_INFO() << "testinternalstate: Case 13 - Selection with Shift+Left";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Select 2 characters with Shift+Left twice
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Left"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Left"), false);

            // Type 'k' to replace selection
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ak");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Selection with Shift+End ---
        if (shouldRunCase(selCopy, 14)) {
            announceCase(14);
            FCITX_INFO() << "testinternalstate: Case 14 - Selection with Shift+End";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Move to start then select to end
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+End"), false);

            // Type 'k' to replace all → "k"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("k");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Selection with Shift+Home ---
        if (shouldRunCase(selCopy, 15)) {
            announceCase(15);
            FCITX_INFO() << "testinternalstate: Case 15 - Selection with Shift+Home";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Select from end to start
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Home"), false);

            // Type 'k' to replace all → "k"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("k");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Word selection with Shift+Ctrl+Right ---
        if (shouldRunCase(selCopy, 16)) {
            announceCase(16);
            FCITX_INFO() << "testinternalstate: Case 16 - Word selection with Shift+Ctrl+Right";

            ic->reset();

            // Type "hello planet" (avoid Telex special letters like 'w')
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("p");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pl");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pla");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plan");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plane");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Move to start and select first word
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Control+Right"), false);

            // Type 'k' to replace selection
            testfrontend->call<ITestFrontend::pushCommitExpectation>("k planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Word selection with Shift+Ctrl+Left ---
        if (shouldRunCase(selCopy, 17)) {
            announceCase(17);
            FCITX_INFO() << "testinternalstate: Case 17 - Word selection with Shift+Ctrl+Left";

            ic->reset();

            // Type "hello planet" (avoid Telex special letters like 'w')
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("p");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pl");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pla");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plan");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plane");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Select last word with Shift+Ctrl+Left
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Control+Left"), false);

            // Type 'k' to replace selection
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello k");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Selection expansion and contraction ---
        if (shouldRunCase(selCopy, 18)) {
            announceCase(18);
            FCITX_INFO() << "testinternalstate: Case 18 - Selection expansion and contraction";

            ic->reset();

            // Type "abcd"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abcd");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("d"), false);

            // Move to start
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);

            // Expand selection: Shift+Right 3 times (selects "abc")
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Right"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Right"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Right"), false);

            // Contract selection: Shift+Left once (selects "ab")
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Left"), false);

            // Type 'k' to replace "ab" → "kcd"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("kcd");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Forward vs backward selection ---
        if (shouldRunCase(selCopy, 19)) {
            announceCase(19);
            FCITX_INFO() << "testinternalstate: Case 19 - Forward vs backward selection";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Create backward selection (anchor at end, cursor at start)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Home"), false);

            // Type 'x' to replace all → "x"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("k");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);

            // Type more
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ky");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("y"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("kyz");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);

            // Create forward selection (anchor at start, cursor at end)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+End"), false);

            // Type 'a' to replace all → "a"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        }

        // ==========================================================
        // DELETE KEY TESTS
        // ==========================================================

        // --- Test: Delete key with no selection ---
        if (shouldRunCase(selCopy, 20)) {
            announceCase(20);
            FCITX_INFO() << "testinternalstate: Case 20 - Delete key with no selection";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Move to start
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);

            // Delete (should delete 'a')
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Delete"), false);

            // Type 'x' → "xbc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("kbc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Delete key with selection ---
        if (shouldRunCase(selCopy, 21)) {
            announceCase(21);
            FCITX_INFO() << "testinternalstate: Case 21 - Delete key with selection";

            ic->reset();

            // Type "abc"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("abc");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Select all
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+a"), false);

            // Delete (should delete selection)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Delete"), false);

            // Type 'x' → "x"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("k");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Delete at text boundaries ---
        if (shouldRunCase(selCopy, 22)) {
            announceCase(22);
            FCITX_INFO() << "testinternalstate: Case 22 - Delete at text boundaries";

            ic->reset();

            // Type "a"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Delete at end (should do nothing)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Delete"), false);

            // Type 'x' → "ax"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ak");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Ctrl+BackSpace word deletion ---
        if (shouldRunCase(selCopy, 31)) {
            announceCase(31);
            FCITX_INFO() << "testinternalstate: Case 31 - Ctrl+BackSpace word deletion";

            ic->reset();

            // Type "hello planet" (avoid Telex special letters like 'w')
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("p");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pl");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pla");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plan");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plane");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Ctrl+BackSpace (delete previous word)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+BackSpace"), false);

            // Type 'k' → "hello k"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello k");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Ctrl+Delete word deletion ---
        if (shouldRunCase(selCopy, 32)) {
            announceCase(32);
            FCITX_INFO() << "testinternalstate: Case 32 - Ctrl+Delete word deletion";

            ic->reset();

            // Type "hello planet" (avoid Telex special letters like 'w')
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("p");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pl");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pla");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plan");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plane");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Move to end of "hello" (before the space).
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+Right"), false);

            // Ctrl+Delete (delete next word including separator)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+Delete"), false);

            // Type 'k' → "hellok"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hellok");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // ==========================================================
        // SELECTION REPLACEMENT TESTS
        // ==========================================================

        // --- Test: Selection replacement with typed text ---
        if (shouldRunCase(selCopy, 23)) {
            announceCase(23);
            FCITX_INFO() << "testinternalstate: Case 23 - Selection replacement with typed text";

            ic->reset();

            // Type "hello"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Select all and type Vietnamese word "xin"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("xin");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
        }

        // ==========================================================
        // UTF-8 EDGE CASE TESTS
        // ==========================================================

        // --- Test: Multi-byte UTF-8 character handling ---
        if (shouldRunCase(selCopy, 24)) {
            announceCase(24);
            FCITX_INFO() << "testinternalstate: Case 24 - Multi-byte UTF-8 character handling";

            ic->reset();

            // Type "việt" (Vietnamese with multiple multi-byte characters)
            testfrontend->call<ITestFrontend::pushCommitExpectation>("v");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("v"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("vi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("viê");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("việ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("j"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("việt");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Move cursor: each arrow should move by one character (not byte)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);

            // Type 'k' → "vikệt" (inserted between i and ệ)
            testfrontend->call<ITestFrontend::pushCommitExpectation>("vikệt");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Vietnamese tone-marked characters in cursor movement ---
        if (shouldRunCase(selCopy, 25)) {
            announceCase(25);
            FCITX_INFO() << "testinternalstate: Case 25 - Vietnamese tone-marked characters in cursor movement";

            ic->reset();

            // Type "chào" (4 characters: c, h, à, o)
            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ch");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("cha");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("chà");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("chào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Press Home then Right twice (should be at |ch|à|o → chà|o)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Home"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Right"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Right"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Right"), false);

            // Type 'k' → "chàko"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("chàko");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // ==========================================================
        // RETURN/TAB HANDLING TESTS
        // ==========================================================

        // --- Test: Return key handling in immediate commit mode ---
        if (shouldRunCase(selCopy, 26)) {
            announceCase(26);
            FCITX_INFO() << "testinternalstate: Case 26 - Return key handling in immediate commit mode";

            ic->reset();

            // Type "hello"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Press Return (should commit newline)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            // Type more text on new line
            testfrontend->call<ITestFrontend::pushCommitExpectation>("p");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pl");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("pla");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plan");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("plane");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
        }

        // --- Test: Tab key handling in immediate commit mode ---
        if (shouldRunCase(selCopy, 27)) {
            announceCase(27);
            FCITX_INFO() << "testinternalstate: Case 27 - Tab key handling in immediate commit mode";

            ic->reset();

            // Type "a"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Press Tab (should commit tab)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Tab"), false);

            // Type "b"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("b");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);
        }

        // ==========================================================
        // STATE CONSISTENCY TESTS
        // ==========================================================

        // --- Test: State consistency after Vietnamese composition ---
        if (shouldRunCase(selCopy, 28)) {
            announceCase(28);
            FCITX_INFO() << "testinternalstate: Case 28 - State consistency after Vietnamese composition";

            ic->reset();

            // Type "chào" (Vietnamese greeting)
            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ch");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("cha");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("chà");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("chào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Navigate back and insert
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);

            // Type space → "chà o"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("chà o");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Test: State consistency after tone changes ---
        if (shouldRunCase(selCopy, 29)) {
            announceCase(29);
            FCITX_INFO() << "testinternalstate: Case 29 - State consistency after tone changes";

            ic->reset();

            // Type "aán" → change tone: "a" → "á" → "à", then "n"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("á");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);

            // Navigate left (cursor: á| → |á)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);

            // Type 'k' before → "ká"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ká");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        // --- Test: Complex sequence - type, select, delete, type again ---
        if (shouldRunCase(selCopy, 30)) {
            announceCase(30);
            FCITX_INFO() << "testinternalstate: Case 30 - Complex sequence";

            ic->reset();

            // Type "hello planet" (avoid Telex special letters like 'w')
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("he");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hel");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hell");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello p");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("p"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello pl");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("l"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello pla");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello plan");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello plane");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello planet");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Select "planet" and delete
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+Control+Left"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"), false);

            // Type Vietnamese "chào"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello ch");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello cha");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello chà");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("hello chào");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Navigate to middle and select all
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+a"), false);

            // Type 'k' → "k"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("k");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("k"), false);
        }

        instance->deactivate();
        dispatcher->schedule([dispatcher, instance]() {
            dispatcher->detach();
            instance->exit();
        });
    });
}

} // namespace

int main(int argc, char **argv) {
    CaseSelection sel;
    for (int i = 1; i < argc; i++) {
        std::string_view arg(argv[i]);
        if (arg == "--list-cases") {
            sel.listCases = true;
        } else if (arg == "--case" && i + 1 < argc) {
            sel.caseId = std::max(0, std::atoi(argv[i + 1]));
            i++;
        } else if (arg.rfind("--case=", 0) == 0) {
            sel.caseId = std::max(0, std::atoi(std::string(arg.substr(7)).c_str()));
        }
    }

    setupTestingEnvironmentPath(TESTING_BINARY_DIR, {"bin"},
                                {TESTING_BINARY_DIR "/test"});

    char arg0[] = "testinternalstate";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,unikey";
    char *fcitxArgv[] = {arg0, arg1, arg2};

    fcitx::Log::setLogRule("default=3,unikey=5");

    if (sel.listCases) {
        printCases();
        return 0;
    }

    Instance instance(FCITX_ARRAY_SIZE(fcitxArgv), fcitxArgv);
    instance.addonManager().registerDefaultLoader(nullptr);

    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    scheduleEvent(&dispatcher, &instance, sel);
    instance.exec();

    return 0;
}
