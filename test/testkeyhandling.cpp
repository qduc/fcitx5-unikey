/*
 * SPDX-FileCopyrightText: 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tests for key handling features:
 * - Backspace handling
 * - Shift+Shift restoration
 * - Shift+Space restoration
 * - Keypad digit support
 * - W at word beginning
 * - Tone changes
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
    std::cout << "Available cases for testkeyhandling:\n";
    std::cout << "  1: Backspace handling in preedit mode\n";
    std::cout << "  2: Backspace at empty preedit\n";
    std::cout << "  3: Complex backspace undo sequence\n";
    std::cout << "  4: Backspace with selection in immediate commit mode\n";
    std::cout << "  5: Shift+Shift keystroke restoration\n";
    std::cout << "  6: Shift+Space keystroke restoration\n";
    std::cout << "  7: Keypad digits for VNI - acute\n";
    std::cout << "  8: Keypad digits for VNI - circumflex\n";
    std::cout << "  9: Keypad digits for VNI - hook above\n";
    std::cout << " 10: W at word beginning (process_w_at_begin=False)\n";
    std::cout << " 11: W at word beginning (process_w_at_begin=True)\n";
    std::cout << " 12: Multiple tone changes\n";
    std::cout << " 13: Double-typing to undo tone\n";
}

void announceCase(int id) {
    // Print unconditionally to make it easy for tooling (ctest wrappers) to
    // detect the failing case even when fcitx logging sinks are not active.
    std::cerr << "testkeyhandling: Case " << id << std::endl;
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

        // Base config for all tests
        RawConfig config;
        config.setValueByPath("SpellCheck", "False");
        config.setValueByPath("Macro", "False");
        config.setValueByPath("AutoNonVnRestore", "False");
        config.setValueByPath("OutputCharset", "Unicode");

        // ==========================================================
        // BACKSPACE HANDLING TESTS
        // ==========================================================

        // --- Test: Backspace handling in preedit mode ---
        // Backspace should progressively undo Vietnamese transformations.
        if (shouldRunCase(selCopy, 1)) {
            announceCase(1);
            FCITX_INFO() << "testkeyhandling: Case 1 - Backspace handling in preedit mode";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "aas" → "ấ"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);

            // Backspace should undo tone: "ấ" → "â"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"), false);

            // Backspace again should undo circumflex: "â" → "a"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"), false);

            // Commit what's left
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Test: Backspace at empty preedit ---
        // When preedit is empty, backspace should pass through to the application.
        if (shouldRunCase(selCopy, 2)) {
            announceCase(2);
            FCITX_INFO() << "testkeyhandling: Case 2 - Backspace at empty preedit";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("hello", 5, 5);
            ic->updateSurroundingText();

            // With empty preedit, backspace should NOT be filtered (pass through).
            // The test frontend will show "accepted: 0" in logs if not filtered.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"), false);
        }

        // --- Test: Complex backspace undo sequence ---
        // Test progressive undo of a more complex word.
        if (shouldRunCase(selCopy, 3)) {
            announceCase(3);
            FCITX_INFO() << "testkeyhandling: Case 3 - Complex backspace undo sequence";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "uw" → "ư", then "ow" → "ươ", then "s" → "ướ"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("u"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);

            // Backspace to undo acute: "ướ" → "ươ"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"), false);

            // Commit
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ươ ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Test: Backspace in immediate commit mode with selection ---
        // When there's a selection, backspace should pass through in immediate mode.
        if (shouldRunCase(selCopy, 4)) {
            announceCase(4);
            FCITX_INFO() << "testkeyhandling: Case 4 - Backspace with selection in immediate commit mode";
            config.setValueByPath("ImmediateCommit", "True");
            config.setValueByPath("InputMethod", "Telex");
            unikey->setConfig(config);

            ic->reset();
            // Text "hello" with selection from 0 to 5 (entire text selected)
            ic->surroundingText().setText("hello", 0, 5);
            ic->updateSurroundingText();

            // Backspace should pass through when there's a selection
            // (the application handles deletion of selected text)
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"), false);
        }

        // ==========================================================
        // SHIFT+SHIFT RESTORATION TESTS
        // ==========================================================

        // --- Test: Shift+Shift keystroke restoration ---
        // Pressing Shift_L then Shift_R (or vice versa) should restore raw ASCII.
        if (shouldRunCase(selCopy, 5)) {
            announceCase(5);
            FCITX_INFO() << "testkeyhandling: Case 5 - Shift+Shift keystroke restoration";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "aa" → "â"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Press Shift_L then Shift_R to trigger restoreKeyStrokes
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift_L"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift_R"), false);

            // The preedit should now be "aa" (restored)
            testfrontend->call<ITestFrontend::pushCommitExpectation>("aa ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // ==========================================================
        // SHIFT+SPACE RESTORATION TESTS
        // ==========================================================

        // --- Test: Shift+Space keystroke restoration ---
        // Shift+Space should restore the original keystrokes (undo conversion).
        if (shouldRunCase(selCopy, 6)) {
            announceCase(6);
            FCITX_INFO() << "testkeyhandling: Case 6 - Shift+Space keystroke restoration";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "aa" → "â"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Shift+Space should restore to "aa " and commit
            testfrontend->call<ITestFrontend::pushCommitExpectation>("aa ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Shift+space"), false);
        }

        // ==========================================================
        // KEYPAD DIGIT TESTS (VNI)
        // ==========================================================

        // --- Test: Keypad digits for VNI - acute tone ---
        // Keypad digits should work the same as regular digits for VNI input.
        if (shouldRunCase(selCopy, 7)) {
            announceCase(7);
            FCITX_INFO() << "testkeyhandling: Case 7 - Keypad digits for VNI - acute";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "VNI");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "a" then KP_1 (acute tone in VNI) → "á"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("KP_1"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("á ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Test: Keypad digits for VNI - circumflex ---
        // VNI: KP_6 should add circumflex (like regular 6).
        if (shouldRunCase(selCopy, 8)) {
            announceCase(8);
            FCITX_INFO() << "testkeyhandling: Case 8 - Keypad digits for VNI - circumflex";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "VNI");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "a" then KP_6 (circumflex in VNI) → "â"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("KP_6"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Test: Keypad digits for VNI - hook above ---
        // VNI: KP_3 should add hook above (hỏi tone).
        if (shouldRunCase(selCopy, 9)) {
            announceCase(9);
            FCITX_INFO() << "testkeyhandling: Case 9 - Keypad digits for VNI - hook above";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "VNI");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "a" then KP_3 (hỏi tone in VNI) → "ả"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("KP_3"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ả ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // ==========================================================
        // W AT WORD BEGINNING TESTS (TELEX)
        // ==========================================================

        // --- Test: W at word beginning (process_w_at_begin=False) ---
        // When process_w_at_begin is False, 'w' at word start should pass through.
        if (shouldRunCase(selCopy, 10)) {
            announceCase(10);
            FCITX_INFO() << "testkeyhandling: Case 10 - W at word beginning (process_w_at_begin=False)";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            config.setValueByPath("ProcessWAtBegin", "False");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "w" at word beginning - should pass through as "w"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);

            // Continue typing "a" - should NOT convert to "ưa" since w was passthrough
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("wa ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);

            // Reset the config
            config.setValueByPath("ProcessWAtBegin", "True");
            unikey->setConfig(config);
        }

        // --- Test: W at word beginning (process_w_at_begin=True) ---
        // When process_w_at_begin is True, 'w' should be processed as horn.
        if (shouldRunCase(selCopy, 11)) {
            announceCase(11);
            FCITX_INFO() << "testkeyhandling: Case 11 - W at word beginning (process_w_at_begin=True)";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            config.setValueByPath("ProcessWAtBegin", "True");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "w" at word beginning - should become "ư"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("w"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ư ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // ==========================================================
        // TONE CHANGE TESTS
        // ==========================================================

        // --- Test: Multiple tone changes ---
        // Changing tones should work correctly.
        if (shouldRunCase(selCopy, 12)) {
            announceCase(12);
            FCITX_INFO() << "testkeyhandling: Case 12 - Multiple tone changes";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "as" → "á", then "f" to change to grave → "à"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("f"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("à ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
        }

        // --- Test: Double-typing to undo ---
        // Telex: typing "ss" should undo the acute tone.
        if (shouldRunCase(selCopy, 13)) {
            announceCase(13);
            FCITX_INFO() << "testkeyhandling: Case 13 - Double-typing to undo tone";
            config.setValueByPath("ImmediateCommit", "False");
            config.setValueByPath("InputMethod", "Telex");
            unikey->setConfig(config);

            ic->reset();
            ic->surroundingText().setText("", 0, 0);
            ic->updateSurroundingText();

            // Type "as" → "á", then "s" again to undo → "as"
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("as ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);
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

    char arg0[] = "testkeyhandling";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,unikey";
    char *fcitxArgv[] = {arg0, arg1, arg2};

    fcitx::Log::setLogRule("default=3,unikey=3");

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
