// SPDX-FileCopyrightText: 2023 yuzu Emulator Project 2023 yuzu Emulator Project 2024 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.suyu.suyu_emu.features.settings.model

interface AbstractBooleanSetting : AbstractSetting {
    fun getBoolean(needsGlobal: Boolean = false): Boolean
    fun setBoolean(value: Boolean)
}
