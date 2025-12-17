/*
 * SPDX-FileCopyrightText: 2012-2018 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef UNIKEY_LOG_H
#define UNIKEY_LOG_H

#include <fcitx-utils/log.h>

namespace fcitx {

FCITX_DECLARE_LOG_CATEGORY(unikey);

} // namespace fcitx

// Debug log macro for unikey module
#define FCITX_UNIKEY_DEBUG() FCITX_LOGC(::fcitx::unikey, Debug)

#endif // UNIKEY_LOG_H
