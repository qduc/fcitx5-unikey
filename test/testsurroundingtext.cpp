/*
 * SPDX-FileCopyrightText: 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "testdir.h"
#include "testconfig.h"
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
    if (sel.caseId != 0 && sel.caseId != id) {
        return false;
    }

    switch (id) {
    case 8:
    case 13:
    case 21:
    case 23:
    case 24:
        return !skipPreeditOnlyCaseInForcedImmediateMode("testsurroundingtext", id);
    default:
        return true;
    }
}

void printCases() {
    // Keep this list in sync with the numbered blocks below.
    std::cout << "Available cases for testsurroundingtext:\n";
    std::cout << "  1: Immediate commit builds word from internal history\n";
    std::cout << "  2: Immediate commit rewrites Vietnamese char internally\n";
    std::cout << "  3: Immediate commit with proper surrounding updates\n";
    std::cout << "  4: Stale/empty surrounding fallback (Firefox-like)\n";
    std::cout << "  5: Truncated surrounding word uses lastImmediateWord fallback\n";
    std::cout << "  6: Immediate commit applies tone to internally-built word\n";
    std::cout << "  7: Active selection in surrounding is ignored in immediate mode\n";
    std::cout << "  8: ModifySurroundingText with cursor==0 should not crash\n";
    std::cout << "  9: Single failure should NOT mark surrounding unreliable\n";
    std::cout << " 10: ImmediateCommit stays internal when surrounding stays stale\n";
    std::cout << " 11: Focus change (reset) clears the internal session\n";
    std::cout << " 12: ImmediateCommit unaffected by stale surrounding; reset starts fresh\n";
    std::cout << " 13: ModifySurroundingText with Vietnamese text present\n";
    std::cout << " 14: ImmediateCommit takes precedence over ModifySurroundingText\n";
    std::cout << " 15: Cursor at word boundary: no rebuild\n";
    std::cout << " 16: Long word built from internal history\n";
    std::cout << " 17: Foreign mixed-content surrounding is ignored\n";
    std::cout << " 18: Cursor at beginning of document\n";
    std::cout << " 19: Rapid keystrokes with stale surrounding\n";
    std::cout << " 20: Backspace edits internal immediate word history\n";
    std::cout << " 21: ModifySurroundingText rebuilds preedit when cursor moves back\n";
    std::cout << " 22: Control characters (newline, tab) are rejected from rebuild\n";
    std::cout << " 23: Raw ASCII pasted from surrounding survives plain space\n";
    std::cout << " 24: Typed VNI digit before space commits converted input\n";
    std::cout << " 25: Immediate commit double-tap to raw, then space\n";
    std::cout << " 26: Immediate commit pasted raw word then space\n";
    std::cout << " 27: Double-tap undo survives stale surrounding\n";
    std::cout << " 28: Immediate commit keeps raw VNI word with repeated tone digits\n";
    std::cout << " 29: Immediate commit keeps raw Telex word with repeated tone keys\n";
    std::cout << " 30: Immediate commit without surrounding capability avoids whole-word duplication\n";
    std::cout << " 31: ImmediateCommit ignores obviously stale surrounding text\n";
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
    setTestConfig(unikey, config);
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

        // --- Case 1: Immediate commit builds the word from internal history ---
        // ImmediateCommit is internal-only: it never reads the surrounding
        // snapshot, so the word must be composed entirely from typed keystrokes.
        // Foreign surrounding text is present but must be ignored.
        if (shouldRunCase(selCopy, 1)) {
            announceCase(1);
            FCITX_INFO() << "testsurroundingtext: Case 1 - Immediate commit builds word from internal history";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Obviously foreign surrounding text must be ignored entirely.
            ic->surroundingText().setText("XYZ", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("n");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ng");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("nga");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            // VNI: 3 = hỏi (ả).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngả");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);
        }

        // --- Case 2: Immediate commit rewrites a Vietnamese char internally ---
        // Build "ngả" purely from internal keystrokes, then retone it to "ngá".
        // No surrounding text is consulted at any point.
        if (shouldRunCase(selCopy, 2)) {
            announceCase(2);
            FCITX_INFO() << "testsurroundingtext: Case 2 - Immediate commit rewrites Vietnamese char internally";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("n");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ng");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("nga");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            // VNI: 3 = hỏi (ả).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngả");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);
            // VNI: 1 = sắc (á); rewrites the existing Vietnamese char.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngá");
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

            // Space is treated as a literal space: the already-committed "ấ"
            // is left untouched (not re-fed into the engine / re-converted),
            // and we simply commit a blank. The field still ends up as "ấ ".
            testfrontend->call<ITestFrontend::pushCommitExpectation>(" ");
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

        // --- Case 5: Truncated/stale surrounding does not derail internal composition ---
        // The app reports only a prefix of the word; immediate mode ignores it
        // and keeps composing from internal history.
        if (shouldRunCase(selCopy, 5)) {
            announceCase(5);
            FCITX_INFO() << "testsurroundingtext: Case 5 - Truncated/stale surrounding ignored; internal composition continues";
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

            // Stale snapshot only shows a prefix of the word; it is ignored.
            ic->surroundingText().setText("e", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ena");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        }

        // --- Case 6: Immediate commit applies tone to the internally-built word ---
        // Build "qua" entirely from keystrokes, then apply a tone. A longer,
        // conflicting surrounding word is reported by the app but must be
        // ignored: the tone is placed using internal history only.
        if (shouldRunCase(selCopy, 6)) {
            announceCase(6);
            FCITX_INFO() << "testsurroundingtext: Case 6 - Immediate commit applies tone to internal word";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Build "qua" from internal keystrokes.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("q");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("q"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("qu");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("qua");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // App reports a conflicting surrounding word; it must be ignored.
            ic->surroundingText().setText("zzzqua", 6, 6);
            ic->updateSurroundingText();

            // VNI: 1 = sắc (á).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("quá");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 7: Active selection in surrounding is ignored in immediate mode ---
        // Immediate-commit never reads surrounding text, so it cannot (and does
        // not) react to an app-reported selection. Composition continues purely
        // from internal history: after "e", typing "x" extends to "ex".
        if (shouldRunCase(selCopy, 7)) {
            announceCase(7);
            FCITX_INFO() << "testsurroundingtext: Case 7 - Active selection in surrounding is ignored";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("e");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);

            // App reports an active selection "xample"; immediate mode ignores it.
            ic->surroundingText().setText("example", 1, 7);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ex");
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

        // --- Case 10: ImmediateCommit stays internal when surrounding stays stale ---
        // ImmediateCommit no longer marks surrounding text as unreliable and
        // falls back to preedit. It keeps composing from its own keystroke
        // history even if the app keeps reporting stale/empty surrounding text.
        if (shouldRunCase(selCopy, 10)) {
            announceCase(10);
            FCITX_INFO() << "testsurroundingtext: Case 10 - ImmediateCommit stays internal with stale surrounding";
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

            // Still empty; the internal session remains authoritative.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("toi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);

            // The next key still commits immediately from internal state.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("tois");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
        }

        // --- Case 11: Focus change (reset) should clear unreliable state ---
        // After an InputContextReset, the unreliable flag should be cleared,
        // giving the new context a fresh start.
        if (shouldRunCase(selCopy, 11)) {
            announceCase(11);
            FCITX_INFO() << "testsurroundingtext: Case 11 - Focus change (reset) clears the internal session";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            // Build an internal session.
            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("xy");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("y"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("xyz");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);

            // Simulate focus change (triggers InputContextReset), which clears
            // the internal immediate-commit session.
            ic->reset();

            // Foreign surrounding text is present but ignored. The fresh word is
            // built from internal keystrokes only; because the session was
            // cleared, the first key starts a brand-new word ("q", not "xyzq").
            ic->surroundingText().setText("qua", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("q");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("q"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("qu");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("qua");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            // VNI: 1 = sắc (á).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("quá");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 12: ImmediateCommit is unaffected by stale surrounding; reset starts fresh ---
        // There is no unreliable-surrounding state in ImmediateCommit anymore.
        // Stale surrounding text does not disable immediate commits, and after a
        // reset the next word is composed purely from internal keystrokes.
        if (shouldRunCase(selCopy, 12)) {
            announceCase(12);
            FCITX_INFO() << "testsurroundingtext: Case 12 - ImmediateCommit unaffected by stale surrounding";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Stale surrounding text does not disable immediate commit.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("m");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ma");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("man");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("manh");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);

            // A reset clears the internal session. Stale surrounding text is
            // present but ignored; the next word is built from keystrokes only.
            ic->reset();
            ic->surroundingText().setText("nga", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("n");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ng");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("nga");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            // VNI: 3 = hỏi (ả).
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

        // --- Case 16: Longer word built entirely from internal history ---
        // Ensure immediate commit handles longer words composed from keystrokes.
        if (shouldRunCase(selCopy, 16)) {
            announceCase(16);
            FCITX_INFO() << "testsurroundingtext: Case 16 - Long word built from internal history";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "nghien" one key at a time.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("n");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ng");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngh");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("nghi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("nghie");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("nghien");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);

            // VNI: 6 adds circumflex to 'e'.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("nghiên");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
        }

        // --- Case 17: Foreign mixed-content surrounding is ignored ---
        // Even when the app reports preceding mixed ASCII + Vietnamese content,
        // immediate commit composes the word from internal keystrokes only.
        if (shouldRunCase(selCopy, 17)) {
            announceCase(17);
            FCITX_INFO() << "testsurroundingtext: Case 17 - Foreign mixed-content surrounding is ignored";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Foreign mixed content present in the field; must be ignored.
            ic->surroundingText().setText("Việt Nam ", 9, 9);
            ic->updateSurroundingText();

            // Build "toi" from internal keystrokes.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("t");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("to");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("toi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);

            // VNI: 6 adds circumflex to 'o' -> "tôi".
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

        // --- Case 20: Backspace edits the internal immediate word history ---
        // Backspace rewrites from the internally tracked word without reading
        // surrounding text as authoritative state.
        if (shouldRunCase(selCopy, 20)) {
            announceCase(20);
            FCITX_INFO() << "testsurroundingtext: Case 20 - Backspace edits immediate word history";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Build "ab" from internal keystrokes.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ab");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("b"), false);

            // Backspace edits the internally tracked word: "ab" -> "a".
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"),
                                                        false);

            // Continue editing the internally tracked word: VNI 1 = sắc.
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
            // When we type another character or space, it should commit.
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

        // --- Case 23: Raw ASCII pasted text should survive plain space ---
        if (shouldRunCase(selCopy, 23)) {
            announceCase(23);
            FCITX_INFO() << "testsurroundingtext: Case 23 - Raw ASCII pasted from surrounding survives plain space";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "False");
            cfg.setValueByPath("ModifySurroundingText", "True");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("ca1", 3, 3);
            ic->updateSurroundingText();

            // Space is literal: the existing raw "ca1" is never pulled back
            // into the engine for conversion, so we just commit a blank and
            // the field stays "ca1 ".
            testfrontend->call<ITestFrontend::pushCommitExpectation>(" ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 24: Typed VNI digit before space commits converted input ---
        if (shouldRunCase(selCopy, 24)) {
            announceCase(24);
            FCITX_INFO() << "testsurroundingtext: Case 24 - Typed VNI digit before space commits converted input";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "False");
            cfg.setValueByPath("ModifySurroundingText", "True");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("ca", 2, 2);
            ic->updateSurroundingText();

            // "ca" + "1" -> "cá": plain space commits the converted word.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 25: Immediate commit double-tap to raw, then space ---
        // Typing a VNI tone twice undoes it ("ca1" -> "cá" -> "ca1"); plain
        // Space then commits a literal blank, leaving the field as "ca1 ".
        // The committed word is never re-fed into the engine on Space.
        if (shouldRunCase(selCopy, 25)) {
            announceCase(25);
            FCITX_INFO() << "testsurroundingtext: Case 25 - Immediate commit double-tap then space";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Each immediate commit rewrites the field; mirror the app's
            // surrounding text after every keystroke.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            ic->surroundingText().setText("c", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ca");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            ic->surroundingText().setText("ca", 2, 2);
            ic->updateSurroundingText();

            // VNI: 1 = sắc -> "cá".
            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
            ic->surroundingText().setText("cá", 2, 2);
            ic->updateSurroundingText();

            // Second 1 undoes the tone back to raw "ca1".
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ca1");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
            ic->surroundingText().setText("ca1", 3, 3);
            ic->updateSurroundingText();

            // Plain Space commits a literal blank; "ca1" stays untouched.
            testfrontend->call<ITestFrontend::pushCommitExpectation>(" ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 26: Immediate commit, pasted raw word then space ---
        // Field already contains "ca1" (e.g. pasted); pressing Space must not
        // pull it back into the engine. It just commits a blank -> "ca1 ".
        if (shouldRunCase(selCopy, 26)) {
            announceCase(26);
            FCITX_INFO() << "testsurroundingtext: Case 26 - Immediate commit pasted word then space";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("ca1", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>(" ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 27: double-tap undo with STALE surrounding on 2nd tone ---
        // The app reports a stale pre-tone snapshot ("ca") right after we
        // committed "cá". The rewrite must NOT trust that stale snapshot and
        // re-apply the tone; it must fall back to the internally tracked word
        // so the repeated tone key correctly undoes to raw "ca1".
        if (shouldRunCase(selCopy, 27)) {
            announceCase(27);
            FCITX_INFO() << "testsurroundingtext: Case 27 - double-tap undo survives stale surrounding";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            ic->surroundingText().setText("c", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ca");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            ic->surroundingText().setText("ca", 2, 2);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
            // STALE: app fails to reflect the tone; surrounding still shows "ca".
            ic->surroundingText().setText("ca", 2, 2);
            ic->updateSurroundingText();

            // Must undo to raw "ca1" (not re-apply the tone to "cá").
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ca1");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 28: Immediate commit keeps raw VNI word with repeated tone digits ---
        // A pasted raw word like "ca1111" should not be rewritten when Space is pressed.
        if (shouldRunCase(selCopy, 28)) {
            announceCase(28);
            FCITX_INFO() << "testsurroundingtext: Case 28 - Immediate commit keeps raw VNI word with repeated tone digits";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            cfg.setValueByPath("InputMethod", "VNI");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("ca1111", 6, 6);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>(" ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 29: Immediate commit keeps raw Telex word with repeated tone keys ---
        // A pasted raw word like "cassss" should not be rewritten when Space is pressed.
        if (shouldRunCase(selCopy, 29)) {
            announceCase(29);
            FCITX_INFO() << "testsurroundingtext: Case 29 - Immediate commit keeps raw Telex word with repeated tone keys";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            cfg.setValueByPath("InputMethod", "Telex");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("cassss", 6, 6);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>(" ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 30: No surrounding capability should not duplicate append-only typing ---
        if (shouldRunCase(selCopy, 30)) {
            announceCase(30);
            FCITX_INFO() << "testsurroundingtext: Case 30 - ImmediateCommit without surrounding capability avoids duplication";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->setCapabilityFlags(CapabilityFlag::NoFlag);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("e");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);

            ic->setCapabilityFlags(CapabilityFlag::SurroundingText);
        }

        // --- Case 31: ImmediateCommit ignores obviously stale surrounding text ---
        // Regression: with ImmediateCommit on, an app that reports wildly stale
        // or incorrect surrounding text (and keeps lying between keystrokes)
        // must not influence the committed result. The output is driven solely
        // by internal keystroke history. Under the old surrounding-bootstrap
        // behavior the lie below ("zzzzzzzz") would have corrupted the result.
        if (shouldRunCase(selCopy, 31)) {
            announceCase(31);
            FCITX_INFO() << "testsurroundingtext: Case 31 - ImmediateCommit ignores obviously stale surrounding";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // The app lies: claims the field already holds a long unrelated word.
            ic->surroundingText().setText("zzzzzzzz", 8, 8);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("t");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Keep feeding garbage surrounding between keystrokes.
            ic->surroundingText().setText("garbage", 7, 7);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::pushCommitExpectation>("to");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            ic->surroundingText().setText("nonsense", 8, 8);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::pushCommitExpectation>("toi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);

            // VNI: 6 adds circumflex to 'o' -> "tôi", from internal history only.
            ic->surroundingText().setText("stillwrong", 10, 10);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::pushCommitExpectation>("tôi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
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
