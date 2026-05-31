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
    return sel.caseId == 0 || sel.caseId == id;
}

void printCases() {
    std::cout << "Available cases for testfirefox:\n";
    std::cout << "  1: Firefox immediate commit with internal state (forward typing)\n";
    std::cout << "  2: Firefox navigation key clears internal state\n";
    std::cout << "  3: Firefox non-ASCII key clears internal state\n";
    std::cout << "  4: Firefox focus change clears internal state\n";
    std::cout << "  5: Firefox selection skips internal rebuild\n";
    std::cout << "  6: Firefox rapid typing chain using internal state\n";
    std::cout << "  7: Firefox ASCII append commits suffix (no duplication)\n";
    std::cout << "  8: Firefox tone rewrite commits suffix (no duplication)\n";
    std::cout << "  9: Firefox Telex ASCII append commits suffix\n";
    std::cout << " 10: Firefox Telex tone rewrite commits suffix\n";
    std::cout << " 11: Firefox URL autocomplete selection rewrite\n";
    std::cout << " 12: Chrome URL bar disables immediate commit rewrite\n";
    std::cout << " 13: Chrome URL bar survives missing selection metadata\n";
    std::cout << " 14: Firefox URL bar stale prefix uses partial rewrite\n";
}

void announceCase(int id) {
    std::cerr << "testfirefox: Case " << id << std::endl;
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

        RawConfig base;
        base.setValueByPath("SpellCheck", "False");
        base.setValueByPath("Macro", "False");
        base.setValueByPath("AutoNonVnRestore", "False");
        base.setValueByPath("InputMethod", "VNI");
        base.setValueByPath("OutputCharset", "Unicode");
        base.setValueByPath("ImmediateCommit", "True");
        base.setValueByPath("ModifySurroundingText", "False");

        // --- Case 1: Firefox Immediate Commit with Internal State (Forward Typing) ---
        if (shouldRunCase(selCopy, 1)) {
            announceCase(1);
            FCITX_INFO() << "testfirefox: Case 1 - Firefox immediate commit with internal state";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

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

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("ấ");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>(" ");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("space"), false);
        }

        // --- Case 2: Firefox Navigation Key Clears Internal State ---
        if (shouldRunCase(selCopy, 2)) {
            announceCase(2);
            FCITX_INFO() << "testfirefox: Case 2 - Firefox navigation key clears state";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("â");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("6"), false);

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Left"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);
        }

        // --- Case 3: Firefox non-ASCII key clears internal state ---
        if (shouldRunCase(selCopy, 3)) {
            announceCase(3);
            FCITX_INFO() << "testfirefox: Case 3 - Firefox non-ASCII key clears state";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

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

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Return"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("1");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
        }

        // --- Case 4: Firefox focus change clears internal state ---
        if (shouldRunCase(selCopy, 4)) {
            announceCase(4);
            FCITX_INFO() << "testfirefox: Case 4 - Firefox focus change clears state";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

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

            icFirefox->reset();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("1");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
        }

        // --- Case 5: Firefox selection skips internal rebuild ---
        if (shouldRunCase(selCopy, 5)) {
            announceCase(5);
            FCITX_INFO() << "testfirefox: Case 5 - Firefox selection skips internal rebuild";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

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

            icFirefox->surroundingText().setText("foo", 1, 3);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
        }

        // --- Case 6: Firefox rapid typing chain using internal state ---
        if (shouldRunCase(selCopy, 6)) {
            announceCase(6);
            FCITX_INFO() << "testfirefox: Case 6 - Firefox rapid typing chain";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("t");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("t"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("o");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("o"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("i");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("i"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("tôi");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("6"), false);
        }

        // --- Case 7: Firefox ASCII append commits suffix (no duplication) ---
        if (shouldRunCase(selCopy, 7)) {
            announceCase(7);
            FCITX_INFO() << "testfirefox: Case 7 - Firefox ASCII append commits suffix";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("b");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("b"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("c"), false);
        }

        // --- Case 8: Firefox tone rewrite commits suffix (no duplication) ---
        if (shouldRunCase(selCopy, 8)) {
            announceCase(8);
            FCITX_INFO() << "testfirefox: Case 8 - Firefox tone rewrite commits suffix";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("c"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
        }

        // --- Case 9: Firefox Telex ASCII append commits suffix ---
        if (shouldRunCase(selCopy, 9)) {
            announceCase(9);
            FCITX_INFO() << "testfirefox: Case 9 - Firefox Telex ASCII append commits suffix";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);

            RawConfig cfg = base;
            cfg.setValueByPath("InputMethod", "Telex");
            configureUnikey(unikey, cfg);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("b");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("b"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("c"), false);
        }

        // --- Case 10: Firefox Telex tone rewrite commits suffix ---
        if (shouldRunCase(selCopy, 10)) {
            announceCase(10);
            FCITX_INFO() << "testfirefox: Case 10 - Firefox Telex tone rewrite commits suffix";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);

            RawConfig cfg = base;
            cfg.setValueByPath("InputMethod", "Telex");
            configureUnikey(unikey, cfg);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("c"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("s"), false);
        }

        // --- Case 11: Firefox URL-bar autocomplete keeps a selected suffix ---
        if (shouldRunCase(selCopy, 11)) {
            announceCase(11);
            FCITX_INFO() << "testfirefox: Case 11 - Firefox URL autocomplete selection rewrite";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("c"), false);

            icFirefox->surroundingText().setText("chatgpt.com", 1, 11);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            icFirefox->surroundingText().setText("calendar.google.com", 2, 19);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
        }

        // --- Case 12: Chrome URL bar must not use delete-surrounding rewrites ---
        if (shouldRunCase(selCopy, 12)) {
            announceCase(12);
            FCITX_INFO() << "testfirefox: Case 12 - Chrome URL bar disables immediate commit rewrite";

            auto uuidChrome =
                testfrontend->call<ITestFrontend::createInputContext>("google-chrome");
            auto *icChrome =
                instance->inputContextManager().findByUUID(uuidChrome);
            FCITX_ASSERT(icChrome);
            icChrome->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

            icChrome->reset();
            icChrome->surroundingText().setText("", 0, 0);
            icChrome->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("Control+space"), false);

            // With ImmediateCommit configured globally, Chrome should still use
            // preedit because Chromium URL bars ignore deleteSurroundingText().
            // No commits should be produced until the composition is finalized.
            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("c"), false);
            icChrome->surroundingText().setText("chatgpt.com", 1, 11);
            icChrome->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("a"), false);
            icChrome->surroundingText().setText("calendar.google.com", 2, 19);
            icChrome->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("1"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá");
            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("Return"), false);
        }

        // --- Case 13: Chrome/Wayland may not report selected autocomplete text ---
        if (shouldRunCase(selCopy, 13)) {
            announceCase(13);
            FCITX_INFO() << "testfirefox: Case 13 - Chrome URL bar survives missing selection metadata";

            auto uuidChrome =
                testfrontend->call<ITestFrontend::createInputContext>("com.google.Chrome");
            auto *icChrome =
                instance->inputContextManager().findByUUID(uuidChrome);
            FCITX_ASSERT(icChrome);
            icChrome->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

            icChrome->reset();
            icChrome->surroundingText().setText("", 0, 0);
            icChrome->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("c"), false);
            // The text contains an autocomplete suffix, but cursor==anchor makes
            // it look like there is no selection. Chromium is still unsupported
            // for surrounding rewrites, so preedit composition must continue.
            icChrome->surroundingText().setText("chatgpt.com", 1, 1);
            icChrome->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("a"), false);
            icChrome->surroundingText().setText("calendar.google.com", 2, 2);
            icChrome->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("1"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("cá");
            testfrontend->call<ITestFrontend::keyEvent>(uuidChrome, Key("Return"), false);
        }

        // --- Case 14: Firefox URL bar may lag one committed character ---
        if (shouldRunCase(selCopy, 14)) {
            announceCase(14);
            FCITX_INFO() << "testfirefox: Case 14 - Firefox URL bar stale prefix uses partial rewrite";

            auto uuidFirefox =
                testfrontend->call<ITestFrontend::createInputContext>("firefox");
            auto *icFirefox =
                instance->inputContextManager().findByUUID(uuidFirefox);
            FCITX_ASSERT(icFirefox);
            icFirefox->setCapabilityFlags(CapabilityFlag::SurroundingText);
            configureUnikey(unikey, base);

            icFirefox->reset();
            icFirefox->surroundingText().setText("", 0, 0);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("Control+space"), false);

            testfrontend->call<ITestFrontend::pushCommitExpectation>("c");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("c"), false);

            // Firefox URL-bar autocomplete can report a long suggestion with no
            // active selection metadata. Appending must not delete+rewrite "c".
            icFirefox->surroundingText().setText("chat.bhtq.ducnq.com/", 1, 1);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("a");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("a"), false);

            // On the tone key Firefox may still report only "c" even though the
            // engine's current word is "ca". Rewriting the whole word would ask
            // Firefox to delete two chars from a stale one-char context and can
            // produce "ccá". Delete only the changed suffix "a" and commit "á".
            icFirefox->surroundingText().setText("c", 1, 1);
            icFirefox->updateSurroundingText();

            testfrontend->call<ITestFrontend::pushCommitExpectation>("á");
            testfrontend->call<ITestFrontend::keyEvent>(uuidFirefox, Key("1"), false);
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

    char arg0[] = "testfirefox";
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
    return 0;
}
