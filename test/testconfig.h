/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FCITX5_UNIKEY_TESTCONFIG_H
#define FCITX5_UNIKEY_TESTCONFIG_H

#include <cstdlib>
#include <fcitx-config/rawconfig.h>
#include <fcitx/addoninstance.h>
#include <iostream>

namespace fcitx {

inline bool forceImmediateCommitForTests() {
    return std::getenv("FCITX_UNIKEY_TEST_FORCE_IMMEDIATE_COMMIT") != nullptr;
}

inline void applyTestConfigOverrides(RawConfig &config) {
    if (forceImmediateCommitForTests()) {
        config.setValueByPath("ImmediateCommit", "True");
    }
}

inline void setTestConfig(AddonInstance *addon, RawConfig config) {
    applyTestConfigOverrides(config);
    addon->setConfig(config);
}

inline bool skipPreeditOnlyCaseInForcedImmediateMode(const char *testName,
                                                     int caseId) {
    if (!forceImmediateCommitForTests()) {
        return false;
    }

    std::cerr << testName << ": Case " << caseId
              << " skipped in forced immediate commit mode" << std::endl;
    return true;
}

} // namespace fcitx

#endif // FCITX5_UNIKEY_TESTCONFIG_H

