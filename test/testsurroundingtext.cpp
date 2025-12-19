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
    std::cout << "  1: Immediate commit rewrite from internally typed ASCII word\n";
    std::cout << "  2: Unicode rebuild (Vietnamese letters in internal buffer)\n";
    std::cout << "  3: Navigation updates cursor; rewrite uses word before cursor\n";
    std::cout << "  4: Selection deletion in immediate commit mode (Ctrl+A + BackSpace)\n";
    std::cout << "  5: Focus reset clears internal model (no rewrite across focus)\n";
    std::cout << "  6: ModifySurroundingText rebuilds preedit after cursor moves back\n";
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

        // --- Case 1: Immediate commit rewrite from internally typed ASCII word ---
        if (shouldRunCase(selCopy, 1)) {
            announceCase(1);
            FCITX_INFO() << "testsurroundingtext: Case 1 - Immediate commit rewrite from internally typed ASCII word";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("n");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ng");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("nga");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngả");
            // VNI: 3 = hỏi (ả).
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);
        }

        // --- Case 2: Unicode rebuild (Vietnamese letters in internal buffer) ---
        if (shouldRunCase(selCopy, 2)) {
            announceCase(2);
            FCITX_INFO() << "testsurroundingtext: Case 2 - Unicode rebuild (Vietnamese letters in internal buffer)";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();

            // Build "ngả" first.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("n");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("n"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ng");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("g"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("nga");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngả");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("3"), false);

            // Now change tone to sắc: "ngá".
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ngá");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("1"), false);
        }

        // --- Case 3: Navigation updates cursor; rewrite uses word before cursor ---
        if (shouldRunCase(selCopy, 3)) {
            announceCase(3);
            FCITX_INFO() << "testsurroundingtext: Case 3 - Navigation updates cursor; rewrite uses word before cursor";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();

            // Type "toi" (immediate commit builds the word incrementally).
            testfrontend->call<ITestFrontend::pushCommitExpectation>("t");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("t"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("to");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("o"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("toi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("i"), false);

            // Move cursor left once: "to|i".
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);

            // Apply VNI 6 at the new cursor position: should rewrite the whole
            // segment "toi" -> "tôi".
            testfrontend->call<ITestFrontend::pushCommitExpectation>("tôi");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
        }


        // --- Case 4: Selection deletion in immediate commit mode (Ctrl+A + BackSpace) ---
        if (shouldRunCase(selCopy, 4)) {
            announceCase(4);
            FCITX_INFO() << "testsurroundingtext: Case 4 - Selection deletion in immediate commit mode";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            cfg.setValueByPath("InputMethod", "Telex");
            configureUnikey(unikey, cfg);

            ic->reset();

            // Type "hello" (incremental commits).
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

            // Select all and delete selection.
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Control+a"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("BackSpace"), false);

            // Now type a new letter; should start fresh.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
        }

        // --- Case 5: Focus reset clears internal model (no rewrite across focus) ---
        if (shouldRunCase(selCopy, 5)) {
            announceCase(5);
            FCITX_INFO() << "testsurroundingtext: Case 5 - Focus reset clears internal model";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "True");
            cfg.setValueByPath("ModifySurroundingText", "False");
            configureUnikey(unikey, cfg);

            ic->reset();
            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);

            // Simulate focus change: clears internal text model.
            ic->reset();

            // VNI digit at "word beginning" should not rewrite previous 'a'
            // across focus; should be committed as a literal digit.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("6");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("6"), false);
        }

        // --- Case 6: ModifySurroundingText rebuilds preedit after cursor moves back ---
        if (shouldRunCase(selCopy, 6)) {
            announceCase(6);
            FCITX_INFO() << "testsurroundingtext: Case 6 - ModifySurroundingText rebuilds preedit after cursor moves back";
            RawConfig cfg = base;
            cfg.setValueByPath("ImmediateCommit", "False");
            cfg.setValueByPath("ModifySurroundingText", "True");
            cfg.setValueByPath("InputMethod", "Telex");
            configureUnikey(unikey, cfg);

            ic->reset();

            // Type "ca" and space in preedit mode -> commits "ca ".
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("c"), false);
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("a"), false);
            testfrontend->call<ITestFrontend::pushCommitExpectation>("ca ");
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("space"), false);

            // Move cursor back before the space: "ca| ".
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("Left"), false);

            // Type Telex tone key "s": should rebuild "ca" into preedit and transform to "cá".
            testfrontend->call<ITestFrontend::keyEvent>(uuid, Key("s"), false);

            // Commit by typing space.
            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá ");
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
