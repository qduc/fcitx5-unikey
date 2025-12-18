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


using namespace fcitx;

namespace {

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

void scheduleEvent(EventDispatcher *dispatcher, Instance *instance) {
    dispatcher->schedule([dispatcher, instance]() {
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
        {
            FCITX_INFO() << "testsurroundingtext: Case 1";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("nga", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngả");
            FCITX_INFO() << "testsurroundingtext: Case 1 key r";
            // VNI: 3 = hỏi (ả).
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);
        }

        // --- Case 2: Unicode rebuild (Vietnamese char in surrounding) ---
        {
            FCITX_INFO() << "testsurroundingtext: Case 2";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("ngả", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngá");
            FCITX_INFO() << "testsurroundingtext: Case 2 key s";
            // VNI: 1 = sắc (á).
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 3: Immediate commit with proper surrounding updates ---
        {
            FCITX_INFO() << "testsurroundingtext: Case 3";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            FCITX_INFO() << "testsurroundingtext: Case 3 key a";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            ic->surroundingText().setText("a", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            FCITX_INFO() << "testsurroundingtext: Case 3 key a";
            // VNI: 6 adds circumflex (â).
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
            ic->surroundingText().setText("â", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ");
            FCITX_INFO() << "testsurroundingtext: Case 3 key s";
            // VNI: 1 = sắc.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
            ic->surroundingText().setText("ấ", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ ");
            FCITX_INFO() << "testsurroundingtext: Case 3 key space";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Case 4: Stale/empty surrounding fallback (Firefox-like) ---
        {
            FCITX_INFO() << "testsurroundingtext: Case 4";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // No surrounding updates between key strokes.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            FCITX_INFO() << "testsurroundingtext: Case 4 key a";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            FCITX_INFO() << "testsurroundingtext: Case 4 key a";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ");
            FCITX_INFO() << "testsurroundingtext: Case 4 key s";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 5: Truncated surrounding word should use lastImmediateWord fallback ---
        {
            FCITX_INFO() << "testsurroundingtext: Case 5";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("e");
            FCITX_INFO() << "testsurroundingtext: Case 5 key e";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("e"), false);

            // Use a neutral key that does not trigger Telex tone processing.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("en");
            FCITX_INFO() << "testsurroundingtext: Case 5 key n";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);

            // Stale snapshot only shows a prefix of the last committed word.
            ic->surroundingText().setText("e", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ena");
            FCITX_INFO() << "testsurroundingtext: Case 5 key a";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        }

        // --- Case 6: Surrounding has extra prefix; trust surrounding for tone placement ---
        {
            FCITX_INFO() << "testsurroundingtext: Case 6";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Build lastImmediateWord = "ua".
            testfrontend->call<ITestFrontend::pushCommitExpectation>("u");
            FCITX_INFO() << "testsurroundingtext: Case 6 key u";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ua");
            FCITX_INFO() << "testsurroundingtext: Case 6 key a";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Now app reports a longer surrounding word: "qua".
            ic->surroundingText().setText("qua", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("quá");
            FCITX_INFO() << "testsurroundingtext: Case 6 key s";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 7: Active selection should skip rebuild/delete and just commit ---
        {
            FCITX_INFO() << "testsurroundingtext: Case 7";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            // Text: "example"; cursor after 'e' (1), selection "xample" (1..7).
            ic->surroundingText().setText("example", 1, 7);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            FCITX_INFO() << "testsurroundingtext: Case 7 key x";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);
        }

        // --- Case 8: ModifySurroundingText with cursor==0 should not underflow/crash ---
        {
            FCITX_INFO() << "testsurroundingtext: Case 8";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "False");
            cfg.setValueByPath("ModifySurroundingText", "True");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // In non-immediate mode, "a" should be committed on Return.
            FCITX_INFO() << "testsurroundingtext: Case 8 key a";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            FCITX_INFO() << "testsurroundingtext: Case 8 key Return";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);
        }

        // --- Case 9: Single failure should NOT mark surrounding as unreliable ---
        // This tests that we need multiple consecutive failures before disabling
        // immediate commit mode.
        {
            FCITX_INFO() << "testsurroundingtext: Case 9";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // First keystroke - immediate commit should work.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            FCITX_INFO() << "testsurroundingtext: Case 9 key a";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Surrounding stays empty (single stale failure).
            // Second keystroke - should still use fallback successfully.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            FCITX_INFO() << "testsurroundingtext: Case 9 key a";
            // VNI: 6 adds circumflex.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);

            // Now provide valid surrounding text - immediate commit should
            // still be enabled (not disabled after one failure).
            ic->surroundingText().setText("â", 1, 1);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ");
            FCITX_INFO() << "testsurroundingtext: Case 9 key s";
            // VNI: 1 = sắc.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 10: Multiple consecutive failures should mark as unreliable ---
        // After kSurroundingFailureThreshold (2) consecutive failures without
        // any successful surrounding reads, the system should fall back to preedit.
        {
            FCITX_INFO() << "testsurroundingtext: Case 10";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // First commit - starts with empty surrounding.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("t");
            FCITX_INFO() << "testsurroundingtext: Case 10 key t";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);

            // Keep surrounding empty (failure 1).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("to");
            FCITX_INFO() << "testsurroundingtext: Case 10 key o";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);

            // Still empty (failure 2 - threshold reached).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("toi");
            FCITX_INFO() << "testsurroundingtext: Case 10 key i";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);

            // After threshold, the system should be in unreliable mode and
            // fall back to preedit. We need to press space to commit in preedit.
            // NOTE: Once in preedit mode, keystrokes don't immediately commit.
            // The 's' key will be added to preedit (building "tois" internally),
            // then we need Return or space to commit.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("s");
            FCITX_INFO() << "testsurroundingtext: Case 10 key Return";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);
        }

        // --- Case 11: Focus change (reset) should clear unreliable state ---
        // After an InputContextReset, the unreliable flag should be cleared,
        // giving the new context a fresh start.
        {
            FCITX_INFO() << "testsurroundingtext: Case 11";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            // First, trigger unreliable state by consecutive failures.
            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("x");
            FCITX_INFO() << "testsurroundingtext: Case 11 key x";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("x"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("xy");
            FCITX_INFO() << "testsurroundingtext: Case 11 key y";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("y"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("xyz");
            FCITX_INFO() << "testsurroundingtext: Case 11 key z";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("z"), false);

            // Now simulate focus change (triggers InputContextReset).
            // This internally calls clearImmediateCommitHistory() which should
            // reset surroundingTextUnreliable_.
            ic->reset();

            // With valid surrounding text, immediate commit should work again.
            ic->surroundingText().setText("qua", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("quá");
            FCITX_INFO() << "testsurroundingtext: Case 11 key s";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 12: Consecutive successes should recover from unreliable ---
        // If surrounding becomes reliable after being marked unreliable,
        // kSurroundingRecoveryThreshold (3) successful operations should
        // restore immediate commit mode.
        {
            FCITX_INFO() << "testsurroundingtext: Case 12";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Trigger failures to enter unreliable state.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("m");
            FCITX_INFO() << "testsurroundingtext: Case 12 key m";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("m"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ma");
            FCITX_INFO() << "testsurroundingtext: Case 12 key a";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("man");
            FCITX_INFO() << "testsurroundingtext: Case 12 key n";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);

            // Now in preedit mode. Commit current preedit.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("h"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("h");
            FCITX_INFO() << "testsurroundingtext: Case 12 key Return";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            // Now start providing valid surrounding text for recovery.
            // We need 3 consecutive successful rebuilds.
            ic->surroundingText().setText("ba", 2, 2);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("s");
            FCITX_INFO() << "testsurroundingtext: Case 12 key Return";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            ic->surroundingText().setText("ca", 2, 2);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("s");
            FCITX_INFO() << "testsurroundingtext: Case 12 key Return";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            ic->surroundingText().setText("da", 2, 2);
            ic->updateSurroundingText();
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("s");
            FCITX_INFO() << "testsurroundingtext: Case 12 key Return";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Return"), false);

            // After 3 successes, immediate commit should be restored.
            ic->surroundingText().setText("nga", 3, 3);
            ic->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngả");
            FCITX_INFO() << "testsurroundingtext: Case 12 key r";
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);
        }

        instance->deactivate();
        dispatcher->schedule([dispatcher, instance]() {
            dispatcher->detach();
            instance->exit();
        });
    });
}

} // namespace

int main() {
    setupTestingEnvironmentPath(TESTING_BINARY_DIR, {"bin"},
                                {TESTING_BINARY_DIR "/test"});

    char arg0[] = "testsurroundingtext";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,unikey";
    char *argv[] = {arg0, arg1, arg2};

    // Keep some logs available when debugging locally; avoid excessive output
    // in CI.
    fcitx::Log::setLogRule("default=3,unikey=3");

    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    EventDispatcher dispatcher;
    dispatcher.attach(&instance.eventLoop());
    scheduleEvent(&dispatcher, &instance);
    instance.exec();

    return 0;
}
