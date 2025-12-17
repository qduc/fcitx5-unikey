/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "unikey-im.h"
#include "unikey-state.h"
#include "charset.h"
#include "inputproc.h"
#include "keycons.h"
#include "unikey-config.h"
#include "unikeyinputcontext.h"
#include "usrkeymap.h"
#include "unikey-utils.h"
#include "unikey-constants.h"
#include "unikey-log.h"
#include "vnconv.h"
#include "vnlexi.h"
#include <cassert>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/fs.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/action.h>
#include <fcitx/addoninstance.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/menu.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <fcntl.h>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fcitx {

FCITX_DEFINE_LOG_CATEGORY(unikey, "unikey");

namespace {

const unsigned int Unikey_OC[] = {CONV_CHARSET_XUTF8,  CONV_CHARSET_TCVN3,
                                  CONV_CHARSET_VNIWIN, CONV_CHARSET_VIQR,
                                  CONV_CHARSET_BKHCM2, CONV_CHARSET_UNI_CSTRING,
                                  CONV_CHARSET_UNIREF, CONV_CHARSET_UNIREF_HEX};
constexpr unsigned int NUM_OUTPUTCHARSET = FCITX_ARRAY_SIZE(Unikey_OC);
static_assert(NUM_OUTPUTCHARSET == UkConvI18NAnnotation::enumLength);

} // namespace



// Implementations of surrounding text rebuild functions are in
// unikey-surrounding-text.cpp

UnikeyEngine::UnikeyEngine(Instance *instance)
    : instance_(instance), factory_([this](InputContext &ic) {
          return new UnikeyState(this, &ic);
      }) {
    instance_->inputContextManager().registerProperty("unikey-state",
                                                      &factory_);

    auto &uiManager = instance_->userInterfaceManager();
    inputMethodAction_ = std::make_unique<SimpleAction>();
    inputMethodAction_->setIcon("document-edit");
    inputMethodAction_->setShortText(_("Input Method"));
    uiManager.registerAction("unikey-input-method", inputMethodAction_.get());

    inputMethodMenu_ = std::make_unique<Menu>();
    inputMethodAction_->setMenu(inputMethodMenu_.get());

    for (UkInputMethod im : {UkTelex, UkVni, UkViqr, UkMsVi, UkUsrIM,
                             UkSimpleTelex, UkSimpleTelex2}) {
        inputMethodSubAction_.emplace_back(std::make_unique<SimpleAction>());
        auto *action = inputMethodSubAction_.back().get();
        action->setShortText(UkInputMethodI18NAnnotation::toString(im));
        action->setCheckable(true);
        uiManager.registerAction(
            "unikey-input-method-" + UkInputMethodToString(im), action);
        connections_.emplace_back(action->connect<SimpleAction::Activated>(
            [this, im](InputContext *ic) {
                config_.im.setValue(im);
                populateConfig();
                safeSaveAsIni(config_, "conf/unikey.conf");
                updateInputMethodAction(ic);
            }));

        inputMethodMenu_->addAction(action);
    }

    charsetAction_ = std::make_unique<SimpleAction>();
    charsetAction_->setShortText(_("Output charset"));
    charsetAction_->setIcon("character-set");
    uiManager.registerAction("unikey-charset", charsetAction_.get());
    charsetMenu_ = std::make_unique<Menu>();
    charsetAction_->setMenu(charsetMenu_.get());

    for (UkConv conv : {UkConv::XUTF8, UkConv::TCVN3, UkConv::VNIWIN,
                        UkConv::VIQR, UkConv::BKHCM2, UkConv::UNI_CSTRING,
                        UkConv::UNIREF, UkConv::UNIREF_HEX}) {
        charsetSubAction_.emplace_back(std::make_unique<SimpleAction>());
        auto *action = charsetSubAction_.back().get();
        action->setShortText(UkConvI18NAnnotation::toString(conv));
        action->setCheckable(true);
        connections_.emplace_back(action->connect<SimpleAction::Activated>(
            [this, conv](InputContext *ic) {
                config_.oc.setValue(conv);
                populateConfig();
                safeSaveAsIni(config_, "conf/unikey.conf");
                updateCharsetAction(ic);
            }));
        uiManager.registerAction("unikey-charset-" + UkConvToString(conv),
                                 action);
        charsetMenu_->addAction(action);
    }
    spellCheckAction_ = std::make_unique<SimpleAction>();
    spellCheckAction_->setLongText(_("Spell check"));
    spellCheckAction_->setIcon("tools-check-spelling");
    connections_.emplace_back(
        spellCheckAction_->connect<SimpleAction::Activated>(
            [this](InputContext *ic) {
                config_.spellCheck.setValue(!*config_.spellCheck);
                populateConfig();
                safeSaveAsIni(config_, "conf/unikey.conf");
                updateSpellAction(ic);
            }));
    uiManager.registerAction("unikey-spell-check", spellCheckAction_.get());
    macroAction_ = std::make_unique<SimpleAction>();
    macroAction_->setLongText(_("Macro"));
    macroAction_->setIcon("edit-find");
    connections_.emplace_back(macroAction_->connect<SimpleAction::Activated>(
        [this](InputContext *ic) {
            config_.macro.setValue(!*config_.macro);
            populateConfig();
            safeSaveAsIni(config_, "conf/unikey.conf");
            updateMacroAction(ic);
        }));
    uiManager.registerAction("unikey-macro", macroAction_.get());

    eventWatchers_.emplace_back(instance_->watchEvent(
        EventType::InputContextSurroundingTextUpdated,
        EventWatcherPhase::PostInputMethod, [this](Event &event) {
            auto &icEvent = static_cast<InputContextEvent &>(event);
            auto *ic = icEvent.inputContext();
            auto *state = ic->propertyFor(&factory_);
            state->mayRebuildStateFromSurroundingText_ = true;
        }));

    reloadConfig();
}

UnikeyEngine::~UnikeyEngine() {}

void UnikeyEngine::activate(const InputMethodEntry & /*entry*/,
                            InputContextEvent &event) {
    auto &statusArea = event.inputContext()->statusArea();
    statusArea.addAction(StatusGroup::InputMethod, inputMethodAction_.get());
    statusArea.addAction(StatusGroup::InputMethod, charsetAction_.get());
    statusArea.addAction(StatusGroup::InputMethod, spellCheckAction_.get());
    statusArea.addAction(StatusGroup::InputMethod, macroAction_.get());

    auto *ic = event.inputContext();
    updateUI(ic);
    auto *state = ic->propertyFor(&factory_);
    if (ic->capabilityFlags().test(CapabilityFlag::SurroundingText)) {
        state->mayRebuildStateFromSurroundingText_ = true;
    }
}

void UnikeyEngine::deactivate(const InputMethodEntry &entry,
                              InputContextEvent &event) {
    if (event.type() == EventType::InputContextSwitchInputMethod) {
        auto *state = event.inputContext()->propertyFor(&factory_);
        state->commit();
    }
    reset(entry, event);
}

void UnikeyEngine::keyEvent(const InputMethodEntry & /*entry*/,
                            KeyEvent &keyEvent) {
    auto *ic = keyEvent.inputContext();
    auto *state = ic->propertyFor(&factory_);
    state->rebuildFromSurroundingText();
    state->keyEvent(keyEvent);
}



void UnikeyEngine::reset(const InputMethodEntry & /*entry*/,
                         InputContextEvent &event) {
    auto *state = event.inputContext()->propertyFor(&factory_);
    state->reset();
    if (event.type() == EventType::InputContextReset) {
        if (event.inputContext()->capabilityFlags().test(
                CapabilityFlag::SurroundingText)) {
            state->mayRebuildStateFromSurroundingText_ = true;
        }
    }
}

void UnikeyEngine::populateConfig() {
    UnikeyOptions ukopt;
    memset(&ukopt, 0, sizeof(ukopt));
    ukopt.macroEnabled = *config_.macro;
    ukopt.spellCheckEnabled = *config_.spellCheck;
    ukopt.autoNonVnRestore = *config_.autoNonVnRestore;
    ukopt.modernStyle = *config_.modernStyle;
    ukopt.freeMarking = *config_.freeMarking;
    im_.setInputMethod(*config_.im);
    im_.setOutputCharset(Unikey_OC[static_cast<int>(*config_.oc)]);
    im_.setOptions(&ukopt);
}

void UnikeyEngine::reloadConfig() {
    readAsIni(config_, "conf/unikey.conf");
    reloadKeymap();
    populateConfig();
    reloadMacroTable();
}

void UnikeyEngine::reloadKeymap() {
    // Keymap need to be reloaded before populateConfig.
    auto keymapFile = StandardPaths::global().open(StandardPathsType::PkgConfig,
                                                   "unikey/keymap.txt");
    if (keymapFile.isValid()) {
        UkLoadKeyMap(keymapFile.fd(), im_.sharedMem()->usrKeyMap);
        im_.sharedMem()->usrKeyMapLoaded = true;
    } else {
        im_.sharedMem()->usrKeyMapLoaded = false;
    }
}

void UnikeyEngine::save() {}

std::string UnikeyEngine::subMode(const InputMethodEntry & /*entry*/,
                                  InputContext & /*inputContext*/) {
    return UkInputMethodI18NAnnotation::toString(*config_.im);
}
void UnikeyEngine::updateMacroAction(InputContext *ic) {
    macroAction_->setChecked(*config_.macro);
    macroAction_->setShortText(*config_.macro ? _("Macro Enabled")
                                              : _("Macro Disabled"));
    macroAction_->update(ic);
}

void UnikeyEngine::updateSpellAction(InputContext *ic) {
    spellCheckAction_->setChecked(*config_.spellCheck);
    spellCheckAction_->setShortText(*config_.spellCheck
                                        ? _("Spell Check Enabled")
                                        : _("Spell Check Disabled"));
    spellCheckAction_->update(ic);
}

void UnikeyEngine::updateInputMethodAction(InputContext *ic) {
    for (size_t i = 0; i < inputMethodSubAction_.size(); i++) {
        inputMethodSubAction_[i]->setChecked(i ==
                                             static_cast<size_t>(*config_.im));
        inputMethodSubAction_[i]->update(ic);
    }
    inputMethodAction_->setLongText(
        UkInputMethodI18NAnnotation::toString(*config_.im));
    inputMethodAction_->update(ic);
}

void UnikeyEngine::updateCharsetAction(InputContext *ic) {
    for (size_t i = 0; i < charsetSubAction_.size(); i++) {
        charsetSubAction_[i]->setChecked(i == static_cast<size_t>(*config_.oc));
        charsetSubAction_[i]->update(ic);
    }
    charsetAction_->setLongText(UkConvI18NAnnotation::toString(*config_.oc));
    charsetAction_->update(ic);
}

void UnikeyEngine::updateUI(InputContext *ic) {
    updateInputMethodAction(ic);
    updateCharsetAction(ic);
    updateMacroAction(ic);
    updateSpellAction(ic);
}



} // namespace fcitx

FCITX_ADDON_FACTORY_V2(unikey, fcitx::UnikeyFactory)
