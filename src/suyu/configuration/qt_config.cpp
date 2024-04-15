// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "input_common/main.h"
#include "qt_config.h"
#include "uisettings.h"

const std::array<int, Settings::NativeButton::NumButtons> QtConfig::default_buttons = {
    Qt::Key_C,    Qt::Key_X, Qt::Key_V,    Qt::Key_Z,  Qt::Key_F,
    Qt::Key_G,    Qt::Key_Q, Qt::Key_E,    Qt::Key_R,  Qt::Key_T,
    Qt::Key_M,    Qt::Key_N, Qt::Key_Left, Qt::Key_Up, Qt::Key_Right,
    Qt::Key_Down, Qt::Key_Q, Qt::Key_E,    0,          0,
    Qt::Key_Q,    Qt::Key_E,
};

const std::array<int, Settings::NativeMotion::NumMotions> QtConfig::default_motions = {
    Qt::Key_7,
    Qt::Key_8,
};

const std::array<std::array<int, 4>, Settings::NativeAnalog::NumAnalogs> QtConfig::default_analogs{{
    {
        Qt::Key_W,
        Qt::Key_S,
        Qt::Key_A,
        Qt::Key_D,
    },
    {
        Qt::Key_I,
        Qt::Key_K,
        Qt::Key_J,
        Qt::Key_L,
    },
}};

const std::array<int, 2> QtConfig::default_stick_mod = {
    Qt::Key_Shift,
    0,
};

const std::array<int, 2> QtConfig::default_ringcon_analogs{{
    Qt::Key_A,
    Qt::Key_D,
}};

QtConfig::QtConfig(const std::string& config_name, const ConfigType config_type)
    : Config(config_type) {
    Initialize(config_name);
    if (config_type != ConfigType::InputProfile) {
        ReadQtValues();
        SaveQtValues();
    }
}

QtConfig::~QtConfig() {
    if (global) {
        QtConfig::SaveAllValues();
    }
}

void QtConfig::ReloadAllValues() {
    Reload();
    ReadQtValues();
    SaveQtValues();
}

void QtConfig::SaveAllValues() {
    SaveValues();
    SaveQtValues();
}

void QtConfig::ReadQtValues() {
    if (global) {
        ReadUIValues();
    }
    ReadQtControlValues();
}

void QtConfig::ReadQtPlayerValues(const std::size_t player_index) {
    std::string player_prefix;
    if (type != ConfigType::InputProfile) {
        player_prefix.append("player_").append(ToString(player_index)).append("_");
    }

    auto& player = Settings::values.players.GetValue()[player_index];
    if (IsCustomConfig()) {
        const auto profile_name =
            ReadStringSetting(std::string(player_prefix).append("profile_name"));
        if (profile_name.empty()) {
            // Use the global input config
            player = Settings::values.players.GetValue(true)[player_index];
            player.profile_name = "";
            return;
        }
    }

    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        auto& player_buttons = player.buttons[i];

        player_buttons = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeButton::mapping[i]), default_param);
        if (player_buttons.empty()) {
            player_buttons = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        auto& player_analogs = player.analogs[i];

        player_analogs = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeAnalog::mapping[i]), default_param);
        if (player_analogs.empty()) {
            player_analogs = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_motions[i]);
        auto& player_motions = player.motions[i];

        player_motions = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeMotion::mapping[i]), default_param);
        if (player_motions.empty()) {
            player_motions = default_param;
        }
    }
}

void QtConfig::ReadHidbusValues() {
    const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
        0, 0, default_ringcon_analogs[0], default_ringcon_analogs[1], 0, 0.05f);
    auto& ringcon_analogs = Settings::values.ringcon_analogs;

    ringcon_analogs = ReadStringSetting(std::string("ring_controller"), default_param);
    if (ringcon_analogs.empty()) {
        ringcon_analogs = default_param;
    }
}

void QtConfig::ReadDebugControlValues() {
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        auto& debug_pad_buttons = Settings::values.debug_pad_buttons[i];

        debug_pad_buttons = ReadStringSetting(
            std::string("debug_pad_").append(Settings::NativeButton::mapping[i]), default_param);
        if (debug_pad_buttons.empty()) {
            debug_pad_buttons = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        auto& debug_pad_analogs = Settings::values.debug_pad_analogs[i];

        debug_pad_analogs = ReadStringSetting(
            std::string("debug_pad_").append(Settings::NativeAnalog::mapping[i]), default_param);
        if (debug_pad_analogs.empty()) {
            debug_pad_analogs = default_param;
        }
    }
}

void QtConfig::ReadQtControlValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    Settings::values.players.SetGlobal(!IsCustomConfig());
    for (std::size_t p = 0; p < Settings::values.players.GetValue().size(); ++p) {
        ReadQtPlayerValues(p);
    }
    if (IsCustomConfig()) {
        EndGroup();
        return;
    }
    ReadDebugControlValues();
    ReadHidbusValues();

    EndGroup();
}

void QtConfig::ReadPathValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Paths));

    UISettings::values.roms_path = ReadStringSetting(std::string("romsPath"));
    UISettings::values.game_dir_deprecated =
        ReadStringSetting(std::string("gameListRootDir"), std::string("."));
    UISettings::values.game_dir_deprecated_deepscan =
        ReadBooleanSetting(std::string("gameListDeepScan"), std::make_optional(false));

    const int gamedirs_size = BeginArray(std::string("gamedirs"));
    for (int i = 0; i < gamedirs_size; ++i) {
        SetArrayIndex(i);
        UISettings::GameDir game_dir;
        game_dir.path = ReadStringSetting(std::string("path"));
        game_dir.deep_scan =
            ReadBooleanSetting(std::string("deep_scan"), std::make_optional(false));
        game_dir.expanded = ReadBooleanSetting(std::string("expanded"), std::make_optional(true));
        UISettings::values.game_dirs.append(game_dir);
    }
    EndArray();

    // Create NAND and SD card directories if empty, these are not removable through the UI,
    // also carries over old game list settings if present
    if (UISettings::values.game_dirs.empty()) {
        UISettings::GameDir game_dir;
        game_dir.path = std::string("SDMC");
        game_dir.expanded = true;
        UISettings::values.game_dirs.append(game_dir);
        game_dir.path = std::string("UserNAND");
        UISettings::values.game_dirs.append(game_dir);
        game_dir.path = std::string("SysNAND");
        UISettings::values.game_dirs.append(game_dir);
        if (UISettings::values.game_dir_deprecated != std::string(".")) {
            game_dir.path = UISettings::values.game_dir_deprecated;
            game_dir.deep_scan = UISettings::values.game_dir_deprecated_deepscan;
            UISettings::values.game_dirs.append(game_dir);
        }
    }
    UISettings::values.recent_files =
        QString::fromStdString(ReadStringSetting(std::string("recentFiles")))
            .split(QStringLiteral(", "), Qt::SkipEmptyParts, Qt::CaseSensitive);

    ReadCategory(Settings::Category::Paths);

    EndGroup();
}

void QtConfig::ReadShortcutValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Shortcuts));

    for (const auto& [name, group, shortcut] : UISettings::default_hotkeys) {
        BeginGroup(group);
        BeginGroup(name);

        // No longer using ReadSetting for shortcut.second as it inaccurately returns a value of 1
        // for WidgetWithChildrenShortcut which is a value of 3. Needed to fix shortcuts the open
        // a file dialog in windowed mode
        UISettings::values.shortcuts.push_back(
            {name,
             group,
             {ReadStringSetting(std::string("KeySeq"), shortcut.keyseq),
              ReadStringSetting(std::string("Controller_KeySeq"), shortcut.controller_keyseq),
              shortcut.context,
              ReadBooleanSetting(std::string("Repeat"), std::optional(shortcut.repeat))}});

        EndGroup(); // name
        EndGroup(); // group
    }

    EndGroup();
}

void QtConfig::ReadUIValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Ui));

    UISettings::values.theme =
        QString::fromStdString(ReadStringSetting("theme", std::string(UISettings::default_theme)));
    UISettings::values.dark_mode_state = static_cast<DarkModeState>(
        ReadIntegerSetting("dark_mode_state", static_cast<int>(DarkModeState::Auto)));

    ReadUIGamelistValues();
    ReadUILayoutValues();
    ReadPathValues();
    ReadScreenshotValues();
    ReadShortcutValues();
    ReadMultiplayerValues();

    ReadCategory(Settings::Category::Ui);
    ReadCategory(Settings::Category::UiGeneral);

    EndGroup();
}

void QtConfig::ReadUIGamelistValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::UiGameList));

    ReadCategory(Settings::Category::UiGameList);

    const int favorites_size = BeginArray("favorites");
    for (int i = 0; i < favorites_size; i++) {
        SetArrayIndex(i);
        UISettings::values.favorited_ids.append(
            ReadUnsignedIntegerSetting(std::string("program_id")));
    }
    EndArray();

    EndGroup();
}

void QtConfig::ReadUILayoutValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::UiGameList));

    ReadCategory(Settings::Category::UiLayout);

    EndGroup();
}

void QtConfig::ReadMultiplayerValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Multiplayer));

    ReadCategory(Settings::Category::Multiplayer);

    // Read ban list back
    int size = BeginArray(std::string("username_ban_list"));
    UISettings::values.multiplayer_ban_list.first.resize(size);
    for (int i = 0; i < size; ++i) {
        SetArrayIndex(i);
        UISettings::values.multiplayer_ban_list.first[i] =
            ReadStringSetting(std::string("username"), std::string(""));
    }
    EndArray();

    size = BeginArray(std::string("ip_ban_list"));
    UISettings::values.multiplayer_ban_list.second.resize(size);
    for (int i = 0; i < size; ++i) {
        UISettings::values.multiplayer_ban_list.second[i] =
            ReadStringSetting("username", std::string(""));
    }
    EndArray();

    EndGroup();
}

void QtConfig::SaveQtValues() {
    if (global) {
        LOG_DEBUG(Config, "Saving global Qt configuration values");
        SaveUIValues();
    } else {
        LOG_DEBUG(Config, "Saving Qt configuration values");
    }
    SaveQtControlValues();

    WriteToIni();
}

void QtConfig::SaveQtPlayerValues(const std::size_t player_index) {
    std::string player_prefix;
    if (type != ConfigType::InputProfile) {
        player_prefix = std::string("player_").append(ToString(player_index)).append("_");
    }

    const auto& player = Settings::values.players.GetValue()[player_index];
    if (IsCustomConfig() && player.profile_name.empty()) {
        // No custom profile selected
        return;
    }

    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeButton::mapping[i]),
                           player.buttons[i], std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeAnalog::mapping[i]),
                           player.analogs[i], std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_motions[i]);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeMotion::mapping[i]),
                           player.motions[i], std::make_optional(default_param));
    }
}

void QtConfig::SaveDebugControlValues() {
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteStringSetting(std::string("debug_pad_").append(Settings::NativeButton::mapping[i]),
                           Settings::values.debug_pad_buttons[i],
                           std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        WriteStringSetting(std::string("debug_pad_").append(Settings::NativeAnalog::mapping[i]),
                           Settings::values.debug_pad_analogs[i],
                           std::make_optional(default_param));
    }
}

void QtConfig::SaveHidbusValues() {
    const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
        0, 0, default_ringcon_analogs[0], default_ringcon_analogs[1], 0, 0.05f);
    WriteStringSetting(std::string("ring_controller"), Settings::values.ringcon_analogs,
                       std::make_optional(default_param));
}

void QtConfig::SaveQtControlValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    Settings::values.players.SetGlobal(!IsCustomConfig());
    for (std::size_t p = 0; p < Settings::values.players.GetValue().size(); ++p) {
        SaveQtPlayerValues(p);
    }
    if (IsCustomConfig()) {
        EndGroup();
        return;
    }
    SaveDebugControlValues();
    SaveHidbusValues();

    EndGroup();
}

void QtConfig::SavePathValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Paths));

    WriteCategory(Settings::Category::Paths);

    WriteStringSetting(std::string("romsPath"), UISettings::values.roms_path);
    BeginArray(std::string("gamedirs"));
    for (int i = 0; i < UISettings::values.game_dirs.size(); ++i) {
        SetArrayIndex(i);
        const auto& game_dir = UISettings::values.game_dirs[i];
        WriteStringSetting(std::string("path"), game_dir.path);
        WriteBooleanSetting(std::string("deep_scan"), game_dir.deep_scan,
                            std::make_optional(false));
        WriteBooleanSetting(std::string("expanded"), game_dir.expanded, std::make_optional(true));
    }
    EndArray();

    WriteStringSetting(std::string("recentFiles"),
                       UISettings::values.recent_files.join(QStringLiteral(", ")).toStdString());

    EndGroup();
}

void QtConfig::SaveShortcutValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Shortcuts));

    // Lengths of UISettings::values.shortcuts & default_hotkeys are same.
    // However, their ordering must also be the same.
    for (std::size_t i = 0; i < UISettings::default_hotkeys.size(); i++) {
        const auto& [name, group, shortcut] = UISettings::values.shortcuts[i];
        const auto& default_hotkey = UISettings::default_hotkeys[i].shortcut;

        BeginGroup(group);
        BeginGroup(name);

        WriteStringSetting(std::string("KeySeq"), shortcut.keyseq,
                           std::make_optional(default_hotkey.keyseq));
        WriteStringSetting(std::string("Controller_KeySeq"), shortcut.controller_keyseq,
                           std::make_optional(default_hotkey.controller_keyseq));
        WriteIntegerSetting(std::string("Context"), shortcut.context,
                            std::make_optional(default_hotkey.context));
        WriteBooleanSetting(std::string("Repeat"), shortcut.repeat,
                            std::make_optional(default_hotkey.repeat));

        EndGroup(); // name
        EndGroup(); // group
    }

    EndGroup();
}

void QtConfig::SaveUIValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Ui));

    WriteCategory(Settings::Category::Ui);
    WriteCategory(Settings::Category::UiGeneral);

    WriteStringSetting("theme", UISettings::values.theme.toStdString(),
                       std::make_optional(std::string(UISettings::default_theme)));
    WriteIntegerSetting("dark_mode_state", static_cast<int>(UISettings::values.dark_mode_state),
                        std::make_optional(static_cast<int>(DarkModeState::Auto)));

    SaveUIGamelistValues();
    SaveUILayoutValues();
    SavePathValues();
    SaveScreenshotValues();
    SaveShortcutValues();
    SaveMultiplayerValues();

    EndGroup();
}

void QtConfig::SaveUIGamelistValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::UiGameList));

    WriteCategory(Settings::Category::UiGameList);

    BeginArray(std::string("favorites"));
    for (int i = 0; i < UISettings::values.favorited_ids.size(); i++) {
        SetArrayIndex(i);
        WriteIntegerSetting(std::string("program_id"), UISettings::values.favorited_ids[i]);
    }
    EndArray(); // favorites

    EndGroup();
}

void QtConfig::SaveUILayoutValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::UiLayout));

    WriteCategory(Settings::Category::UiLayout);

    EndGroup();
}

void QtConfig::SaveMultiplayerValues() {
    BeginGroup(std::string("Multiplayer"));

    WriteCategory(Settings::Category::Multiplayer);

    // Write ban list
    BeginArray(std::string("username_ban_list"));
    for (std::size_t i = 0; i < UISettings::values.multiplayer_ban_list.first.size(); ++i) {
        SetArrayIndex(static_cast<int>(i));
        WriteStringSetting(std::string("username"),
                           UISettings::values.multiplayer_ban_list.first[i]);
    }
    EndArray(); // username_ban_list

    BeginArray(std::string("ip_ban_list"));
    for (std::size_t i = 0; i < UISettings::values.multiplayer_ban_list.second.size(); ++i) {
        SetArrayIndex(static_cast<int>(i));
        WriteStringSetting(std::string("ip"), UISettings::values.multiplayer_ban_list.second[i]);
    }
    EndArray(); // ip_ban_list

    EndGroup();
}

std::vector<Settings::BasicSetting*>& QtConfig::FindRelevantList(Settings::Category category) {
    auto& map = Settings::values.linkage.by_category;
    if (map.contains(category)) {
        return Settings::values.linkage.by_category[category];
    }
    return UISettings::values.linkage.by_category[category];
}

void QtConfig::ReadQtControlPlayerValues(std::size_t player_index) {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    ReadPlayerValues(player_index);
    ReadQtPlayerValues(player_index);

    EndGroup();
}

void QtConfig::SaveQtControlPlayerValues(std::size_t player_index) {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    LOG_DEBUG(Config, "Saving players control configuration values");
    SavePlayerValues(player_index);
    SaveQtPlayerValues(player_index);

    EndGroup();

    WriteToIni();
}
