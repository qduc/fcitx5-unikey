/*
 * SPDX-FileCopyrightText: 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
    // Keep this list in sync with the numbered blocks below.
    std::cout << "Available cases for testsurroundingtext:\n";
    std::cout << "  1: Immediate commit rewrite from ASCII surrounding\n";
    std::cout << "  2: Unicode rebuild (Vietnamese char in surrounding)\n";
    std::cout << "  3: Immediate commit with proper surrounding updates\n";
    std::cout << "  4: Stale/empty surrounding fallback (Firefox-like)\n";
    std::cout << "  5: Truncated surrounding word uses lastImmediateWord fallback\n";
    std::cout << "  6: Surrounding has extra prefix; trust surrounding for tone\n";
    std::cout << "  7: Active selection skips rebuild/delete and just commits\n";
    std::cout << "  8: ModifySurroundingText with cursor==0 should not crash\n";
    std::cout << "  9: Single failure should NOT mark surrounding unreliable\n";
    std::cout << " 10: Multiple consecutive failures should mark as unreliable\n";
    std::cout << " 11: Focus change (reset) clears unreliable state\n";
    std::cout << " 12: Consecutive successes recover from unreliable\n";
    std::cout << " 13: ModifySurroundingText with Vietnamese text present\n";
    std::cout << " 14: ImmediateCommit takes precedence over ModifySurroundingText\n";
    std::cout << " 15: Cursor at word boundary: no rebuild\n";
    std::cout << " 16: Long word near MAX_LENGTH_VNWORD\n";
    std::cout << " 17: Mixed ASCII + Vietnamese in surrounding\n";
    std::cout << " 18: Cursor at beginning of document\n";
    std::cout << " 19: Rapid keystrokes with stale surrounding\n";
    std::cout << " 20: Backspace clears immediate word history\n";
    std::cout << " 21: ModifySurroundingText rebuilds preedit when cursor moves back\n";
    std::cout << " 22: Control characters (newline, tab) are rejected from rebuild\n";
    std::cout << " 23: Firefox immediate commit with internal state (forward typing)\n";
    std::cout << " 24: Firefox navigation key clears internal state\n";
    std::cout << " 25: Firefox non-ASCII key clears internal state\n";
    std::cout << " 26: Firefox focus change clears internal state\n";
    std::cout << " 27: Firefox selection skips internal rebuild\n";
    std::cout << " 28: Firefox rapid typing chain using internal state\n";
}

void announceCase(int id) {
    // Print unconditionally to make it easy for tooling (ctest wrappers) to
    // detect the failing case even when fcitx logging sinks are not active.
    std::cerr << "testsurroundingtext: Case " << id << std::endl;
}

void setupInputMethodGroup(Instance *instance) {
    auto defaultGroup = instance->inputMethodManager().currentGroup();
    defaultGroup.inputMethodList().clear();
    defaultGroup.inputMethodList().push_back(InputMethodGroupItem("keyboard-us"));
    defaultGroup.inputMethodList().push_back(InputMethodGroupItem("unikey"));
    defaultGroup.setDefaultInputMethod("");
    instance->inputMethodManager().setGroup(defaultGroup);
}

void configureUnikey(AddonInstance *unikey, const RawConfig &config) {
    // The addon interface is AddonInstance; setConfig is virtual on AddonInstance.
    unikey->setConfig(config);
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

        // Base config: deterministic behavior.
        RawConfig base;
        base.setValueByPath("SpellCheck", "False");
        base.setValueByPath("Macro", "False");
        base.setValueByPath("AutoNonVnRestore", "False");
        // Use VNI to avoid collisions with English words (Telex tone keys are letters).
        base.setValueByPath("InputMethod", "VNI");
        base.setValueByPath("OutputCharset", "Unicode");

        // --- Case 1: Immediate commit rewrite from ASCII surrounding ---
        if (shouldRunCase(selCopy, 1)) {
            announceCase(1);
            FCITX_INFO() << "testsurroundingtext: Case 1 - Immediate commit rewrite from ASCII surrounding";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("nga", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngả");
            // VNI: 3 = hỏi (ả).
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);
        }

        // --- Case 2: Unicode rebuild (Vietnamese char in surrounding) ---
        if (shouldRunCase(selCopy, 2)) {
            announceCase(2);
            FCITX_INFO() << "testsurroundingtext: Case 2 - Unicode rebuild (Vietnamese char in surrounding)";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("ngả", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngá");
            // VNI: 1 = sắc (á).
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 3: Immediate commit with proper surrounding updates ---
        if (shouldRunCase(selCopy, 3)) {
            announceCase(3);
            FCITX_INFO() << "testsurroundingtext: Case 3 - Immediate commit with proper surrounding updates";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            ic->surroundingText().setText("a", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            // VNI: 6 adds circumflex (â).
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
            ic->surroundingText().setText("â", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ");
            // VNI: 1 = sắc.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
            ic->surroundingText().setText("ấ", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 4: Stale/empty surrounding fallback (Firefox-like) ---
        if (shouldRunCase(selCopy, 4)) {
            announceCase(4);
            FCITX_INFO() << "testsurroundingtext: Case 4 - Stale/empty surrounding fallback (Firefox-like)";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // No surrounding updates between key strokes.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 5: Truncated surrounding word should use lastImmediateWord fallback ---
        if (shouldRunCase(selCopy, 5)) {
            announceCase(5);
            FCITX_INFO() << "testsurroundingtext: Case 5 - Truncated surrounding word uses lastImmediateWord fallback";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("e");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);

            // Use a neutral key that does not trigger Telex tone processing.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("en");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);

            // Stale snapshot only shows a prefix of the last committed word.
            ic->surroundingText().setText("e", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ena");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        }

        // --- Case 6: Surrounding has extra prefix; trust surrounding for tone placement ---
        if (shouldRunCase(selCopy, 6)) {
            announceCase(6);
            FCITX_INFO() << "testsurroundingtext: Case 6 - Surrounding has extra prefix; trust surrounding for tone";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Build lastImmediateWord = "ua".
            testfrontend->call<ITestFrontend::pushCommitExpectation>("u");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ua");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Now app reports a longer surrounding word: "qua".
            ic->surroundingText().setText("qua", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("quá");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 7: Active selection should skip rebuild/delete and just commit ---
        if (shouldRunCase(selCopy, 7)) {
            announceCase(7);
            FCITX_INFO() << "testsurroundingtext: Case 7 - Active selection skips rebuild/delete and just commits";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Text: "example"; cursor after 'e' (1), selection "xample" (1..7).
            ic->surroundingText().setText("example", 1, 7);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        }

        // --- Case 8: ModifySurroundingText with cursor==0 should not underflow/crash ---
        if (shouldRunCase(selCopy, 8)) {
            announceCase(8);
            FCITX_INFO() << "testsurroundingtext: Case 8 - ModifySurroundingText with cursor==0 should not crash";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "False");
            cfg.setValueByPath("ModifySurroundingText", "True");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // In non-immediate mode, "a" should be committed on Return.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);
        }

        // --- Case 9: Single failure should NOT mark surrounding as unreliable ---
        // This tests that we need multiple consecutive failures before disabling
        // immediate commit mode.
        if (shouldRunCase(selCopy, 9)) {
            announceCase(9);
            FCITX_INFO() << "testsurroundingtext: Case 9 - Single failure should NOT mark surrounding unreliable";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // First keystroke - immediate commit should work.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Surrounding stays empty (single stale failure).
            // Second keystroke - should still use fallback successfully.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            // VNI: 6 adds circumflex.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);

            // Now provide valid surrounding text - immediate commit should
            // still be enabled (not disabled after one failure).
            ic->surroundingText().setText("â", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ");
            // VNI: 1 = sắc.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 10: Multiple consecutive failures should mark as unreliable ---
        // After kSurroundingFailureThreshold (2) consecutive failures without
        // any successful surrounding reads, the system should fall back to preedit.
        if (shouldRunCase(selCopy, 10)) {
            announceCase(10);
            FCITX_INFO() << "testsurroundingtext: Case 10 - Multiple consecutive failures should mark as unreliable";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // First commit - starts with empty surrounding.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("t");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Keep surrounding empty (failure 1).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("to");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Still empty (failure 2 - threshold reached).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("toi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);

            // After threshold, the system should be in unreliable mode and
            // fall back to preedit. We need to press space to commit in preedit.
            // NOTE: Once in preedit mode, keystrokes don't immediately commit.
            // The 's' key will be added to preedit (building "tois" internally),
            // then we need Return or space to commit.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("s");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);
        }

        // --- Case 11: Focus change (reset) should clear unreliable state ---
        // After an InputContextReset, the unreliable flag should be cleared,
        // giving the new context a fresh start.
        if (shouldRunCase(selCopy, 11)) {
            announceCase(11);
            FCITX_INFO() << "testsurroundingtext: Case 11 - Focus change (reset) clears unreliable state";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            // First, trigger unreliable state by consecutive failures.
            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("xy");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("y"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("xyz");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);

            // Now simulate focus change (triggers InputContextReset).
            // This internally calls clearImmediateCommitHistory() which should
            // reset surroundingTextUnreliable_.
            ic->reset();

            // With valid surrounding text, immediate commit should work again.
            ic->surroundingText().setText("qua", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("quá");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 12: Consecutive successes should recover from unreliable ---
        // If surrounding becomes reliable after being marked unreliable,
        // kSurroundingRecoveryThreshold (3) successful operations should
        // restore immediate commit mode.
        if (shouldRunCase(selCopy, 12)) {
            announceCase(12);
            FCITX_INFO() << "testsurroundingtext: Case 12 - Consecutive successes recover from unreliable";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Trigger failures to enter unreliable state.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("m");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ma");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("man");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);

            // Now in preedit mode. Commit current preedit.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            // Now start providing valid surrounding text for recovery.
            // We need 3 consecutive successful rebuilds.
            ic->surroundingText().setText("ba", 2, 2);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("s");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            ic->surroundingText().setText("ca", 2, 2);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("s");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            ic->surroundingText().setText("da", 2, 2);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("s");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            // After 3 successes, immediate commit should be restored.
            ic->surroundingText().setText("nga", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngả");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);
        }

        // ==========================================================
        // ADDITIONAL HIGH-VALUE TESTS
        // ==========================================================

        // --- Case 13: ModifySurroundingText with Vietnamese text present ---
        // When modifySurroundingText is enabled, existing Vietnamese text
        // should be rebuilt and modified correctly.
        if (shouldRunCase(selCopy, 13)) {
            announceCase(13);
            FCITX_INFO() << "testsurroundingtext: Case 13 - ModifySurroundingText with Vietnamese text present";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "False");
            cfg.setValueByPath("ModifySurroundingText", "True");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Set up Vietnamese text with cursor at end
            ic->surroundingText().setText("nga", 3, 3);
            ic->updateSurroundingText();

            // Type 's' to add tone - should work with ModifySurroundingText
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngá");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"),
                                                        false);
        }

        // --- Case 14: ImmediateCommit takes precedence over
        // ModifySurroundingText --- When both are enabled, immediateCommit
        // behavior should apply.
        if (shouldRunCase(selCopy, 14)) {
            announceCase(14);
            FCITX_INFO() << "testsurroundingtext: Case 14 - ImmediateCommit takes precedence over ModifySurroundingText";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "True");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Build a word with surrounding updates
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            ic->surroundingText().setText("a", 1, 1);
            ic->updateSurroundingText();

            // Add circumflex - should work in immediate commit mode
            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            // VNI: 6 adds circumflex
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
        }

        // --- Case 15: Surrounding text with word boundary at cursor ---
        // When cursor is right after a word boundary, no rebuild should occur.
        if (shouldRunCase(selCopy, 15)) {
            announceCase(15);
            FCITX_INFO() << "testsurroundingtext: Case 15 - Cursor at word boundary: no rebuild";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Text with space before cursor
            ic->surroundingText().setText("hello ", 6, 6);
            ic->updateSurroundingText();

            // Type 'a' - should just commit 'a' without rebuild
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        }

        // --- Case 16: Very long word approaching MAX_LENGTH_VNWORD ---
        // Ensure the rebuild logic handles long words correctly.
        if (shouldRunCase(selCopy, 16)) {
            announceCase(16);
            FCITX_INFO() << "testsurroundingtext: Case 16 - Long word near MAX_LENGTH_VNWORD";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // 15-character word (MAX_LENGTH_VNWORD is around 18)
            ic->surroundingText().setText("nghien", 6, 6);
            ic->updateSurroundingText();

            // Add tone - should rebuild and work correctly
            testfrontend->call<ITestFrontend::pushCommitExpectation>(
                "nghiên");
            // VNI: 6 adds circumflex to 'e'
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
        }

        // --- Case 17: Mixed ASCII and Vietnamese in surrounding ---
        // Ensure proper word boundary detection with mixed content.
        if (shouldRunCase(selCopy, 17)) {
            announceCase(17);
            FCITX_INFO() << "testsurroundingtext: Case 17 - Mixed ASCII + Vietnamese in surrounding";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Vietnamese word followed by cursor
            ic->surroundingText().setText("Việt Nam toi", 12, 12);
            ic->updateSurroundingText();

            // Add tone to "toi" -> "tôi"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("tôi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
        }

        // --- Case 18: Cursor at beginning of document ---
        // Ensure no underflow when cursor is at position 0.
        if (shouldRunCase(selCopy, 18)) {
            announceCase(18);
            FCITX_INFO() << "testsurroundingtext: Case 18 - Cursor at beginning of document";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Cursor at position 0 with text after cursor
            ic->surroundingText().setText("hello", 0, 0);
            ic->updateSurroundingText();

            // Should just commit 'a' without trying to access text before
            // cursor
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        }

        // --- Case 19: Rapid consecutive keystrokes with stale surrounding ---
        // Simulating fast typing where surrounding text can't keep up.
        if (shouldRunCase(selCopy, 19)) {
            announceCase(19);
            FCITX_INFO() << "testsurroundingtext: Case 19 - Rapid keystrokes with stale surrounding";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Rapid typing: "toi6" without surrounding updates
            testfrontend->call<ITestFrontend::pushCommitExpectation>("t");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("to");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("toi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);

            // Add circumflex - using fallback since no surrounding updates
            testfrontend->call<ITestFrontend::pushCommitExpectation>("tôi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
        }

        // --- Case 20: Backspace clears immediate word history ---
        // After explicit deletion, immediate word tracking should be cleared.
        if (shouldRunCase(selCopy, 20)) {
            announceCase(20);
            FCITX_INFO() << "testsurroundingtext: Case 20 - Backspace clears immediate word history";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("a", 1, 1);
            ic->updateSurroundingText();

            // Type 'b'
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);

            // Now backspace
            ic->surroundingText().setText("ab", 2, 2);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                        false);

            // Type new character - should start fresh context
            ic->surroundingText().setText("a", 1, 1);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::pushCommitExpectation>("á");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 21: ModifySurroundingText mode rebuilds preedit when cursor moves back ---
        // This tests the scenario: type "ca ", move cursor back to "ca|", type "s" -> expect "cá" in preedit
        if (shouldRunCase(selCopy, 21)) {
            announceCase(21);
            FCITX_INFO() << "testsurroundingtext: Case 21 - ModifySurroundingText rebuilds from cursor position";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "False");  // Use preedit mode
            cfg.setValueByPath("ModifySurroundingText", "True");  // Enable rebuild
            cfg.setValueByPath("InputMethod", "Telex");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "ca" and space - should commit "ca " in preedit mode
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ca ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);

            // Simulate cursor moving back to position after "ca" (before space)
            // Text is "ca ", cursor at position 2 (after "ca")
            ic->surroundingText().setText("ca ", 2, 2);
            ic->updateSurroundingText();

            // Type "s" - should delete "ca" from surrounding, rebuild preedit with "ca",
            // then process "s" -> "cás" which becomes "cá" with Telex
            // Since we're in preedit mode, this will show as preedit, not immediate commit
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);

            // The preedit should now show "cá" (not yet committed)
            // When we type another character or space, it should commit
            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 22: Control characters (newline, tab) should NOT be rebuilt from surrounding ---
        if (shouldRunCase(selCopy, 22)) {
            announceCase(22);
            FCITX_INFO() << "testsurroundingtext: Case 22 - Control characters rejected from rebuild";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Surrounding text contains a newline before cursor
            ic->surroundingText().setText("\n", 1, 1);
            ic->updateSurroundingText();

            // Type 'c' - should NOT include the newline in rebuild
            // Expected: just commit 'c', not '\nc'
            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);

            // Similarly test tab character
            ic->reset();
            ic->surroundingText().setText("\t", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        }

        // --- Case 23: Firefox Immediate Commit with Internal State (Forward Typing) ---
        // With the new Firefox support, immediate commit mode is enabled using
        // internal state tracking instead of querying Firefox's unreliable surrounding text.
        // Test forward typing: a→ă→ắ (VNI input method)
        if (shouldRunCase(selCopy, 23)) {
            announceCase(23);
            FCITX_INFO() << "testsurroundingtext: Case 23 - Firefox immediate commit with internal state";

            // Create Firefox context
            auto uuidFirefox = testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox = instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);

            // Enable immediate commit mode for Firefox (now supported via internal state)
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            // Toggle Unikey ON
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            // Type "a" → commits "a", records in lastImmediateWord_
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            // Firefox returns STALE/empty surrounding (simulating Wayland bug)
            // Don't update surrounding text - keep it empty or stale
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            // Type "6" (VNI breve) → should rebuild from internal state "a"
            // Deletes "a" (-1 char), commits "â"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("6"), false);

            // Firefox still returns stale surrounding
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            // Type "1" (VNI acute tone) → rebuilds from internal "â"
            // Deletes "â" (-1 char), commits "ắ"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);

            // Type space to complete the word
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ ");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("space"), false);
        }

        // --- Case 24: Firefox Navigation Key Clears Internal State ---
        // Navigation keys should clear the internal state so we don't incorrectly
        // rewrite at a new cursor position
        if (shouldRunCase(selCopy, 24)) {
            announceCase(24);
            FCITX_INFO() << "testsurroundingtext: Case 24 - Firefox navigation key clears state";

            // Create Firefox context
            auto uuidFirefox = testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox = instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);

            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            // Toggle Unikey ON
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            // Type "a" → commits "a"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            // Type "6" (breve) → commits "â"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("6"), false);

            // Press Left arrow - should clear internal state
            // Arrow key is passed through, doesn't commit anything
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Left"), false);

            // Now type "a" - should be a fresh "a", NOT merged with previous "â"
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);
        }

        // --- Case 25: Firefox non-ASCII key clears internal state ---
        // Non-ASCII keys should clear internal state to avoid incorrect rewrites.
        if (shouldRunCase(selCopy, 25)) {
            announceCase(25);
            FCITX_INFO() << "testsurroundingtext: Case 25 - Firefox non-ASCII key clears state";

            auto uuidFirefox = testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox = instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);

            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("6"), false);

            // Non-ASCII key (Return) should clear internal state.
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Return"), false);

            // With state cleared, "1" should not merge with previous "â".
            testfrontend->call<ITestFrontend::pushCommitExpectation>("1");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
        }

        // --- Case 26: Firefox focus change clears internal state ---
        if (shouldRunCase(selCopy, 26)) {
            announceCase(26);
            FCITX_INFO() << "testsurroundingtext: Case 26 - Firefox focus change clears state";

            auto uuidFirefox = testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox = instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);

            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("6"), false);

            // Simulate focus change; should clear internal state.
            icFirefox->reset();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("1");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
        }

        // --- Case 27: Firefox selection skips internal rebuild ---
        if (shouldRunCase(selCopy, 27)) {
            announceCase(27);
            FCITX_INFO() << "testsurroundingtext: Case 27 - Firefox selection skips internal rebuild";

            auto uuidFirefox = testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox = instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);

            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("6"), false);

            // Mark a selection; internal rebuild should be skipped.
            icFirefox->surroundingText().setText("foo", 1, 3);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("1");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
        }

        // --- Case 28: Firefox rapid typing chain using internal state ---
        if (shouldRunCase(selCopy, 28)) {
            announceCase(28);
            FCITX_INFO() << "testsurroundingtext: Case 28 - Firefox rapid typing chain";

            auto uuidFirefox = testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox = instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);

            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("t");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("t"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("to");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("o"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("toi");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("i"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("tôi");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("6"), false);
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

    char arg0[] = "testsurroundingtext";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,unikey";
    char *fcitxArgv[] = {arg0, arg1, arg2};

    // Keep some logs available when debugging locally; avoid excessive output
    // in CI.
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
