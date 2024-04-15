// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <vector>
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include "common/common_types.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "configuration/qt_config.h"

using Settings::Category;
using Settings::ConfirmStop;
using Settings::DarkModeState;
using Settings::Setting;
using Settings::SwitchableSetting;

#ifndef CANNOT_EXPLICITLY_INSTANTIATE
namespace Settings {
extern template class Setting<bool>;
extern template class Setting<std::string>;
extern template class Setting<u16, true>;
extern template class Setting<u32>;
extern template class Setting<u8, true>;
extern template class Setting<u8>;
extern template class Setting<unsigned long long>;
} // namespace Settings
#endif

namespace UISettings {

struct ContextualShortcut {
    std::string keyseq;
    std::string controller_keyseq;
    int context;
    bool repeat;
};

struct Shortcut {
    std::string name;
    std::string group;
    ContextualShortcut shortcut;
};

static constexpr std::string_view default_theme{":/default"};

using Themes = std::array<std::pair<const char*, const char*>, 6>;
extern const Themes included_themes;

struct GameDir {
    std::string path;
    bool deep_scan = false;
    bool expanded = false;
    bool operator==(const GameDir& rhs) const {
        return path == rhs.path;
    }
    bool operator!=(const GameDir& rhs) const {
        return !operator==(rhs);
    }
};

struct Values {
    Settings::Linkage linkage{1000};

    QByteArray geometry;
    QByteArray state;

    QByteArray renderwindow_geometry;

    QByteArray gamelist_header_state;

    QByteArray microprofile_geometry;
    Setting<bool> microprofile_visible{linkage, false, "microProfileDialogVisible",
                                       Category::UiLayout};

    Setting<bool> single_window_mode{linkage, true, "singleWindowMode", Category::Ui};
    Setting<bool> fullscreen{linkage, false, "fullscreen", Category::Ui};
    Setting<bool> display_titlebar{linkage, true, "displayTitleBars", Category::Ui};
    Setting<bool> show_filter_bar{linkage, true, "showFilterBar", Category::Ui};
    Setting<bool> show_status_bar{linkage, true, "showStatusBar", Category::Ui};

    SwitchableSetting<ConfirmStop> confirm_before_stopping{linkage,
                                                           ConfirmStop::Ask_Always,
                                                           "confirmStop",
                                                           Category::UiGeneral,
                                                           Settings::Specialization::Default,
                                                           true,
                                                           true};

    Setting<bool> first_start{linkage, true, "firstStart", Category::Ui};
    Setting<bool> pause_when_in_background{linkage,
                                           false,
                                           "pauseWhenInBackground",
                                           Category::UiGeneral,
                                           Settings::Specialization::Default,
                                           true,
                                           true};
    Setting<bool> mute_when_in_background{linkage,
                                          false,
                                          "muteWhenInBackground",
                                          Category::UiAudio,
                                          Settings::Specialization::Default,
                                          true,
                                          true};
    Setting<bool> hide_mouse{
        linkage, true, "hideInactiveMouse", Category::UiGeneral, Settings::Specialization::Default,
        true,    true};
    Setting<bool> controller_applet_disabled{linkage, false, "disableControllerApplet",
                                             Category::UiGeneral};
    // Set when Vulkan is known to crash the application
    bool has_broken_vulkan = false;

    Setting<bool> select_user_on_boot{linkage,
                                      false,
                                      "select_user_on_boot",
                                      Category::UiGeneral,
                                      Settings::Specialization::Default,
                                      true,
                                      true};
    Setting<bool> disable_web_applet{linkage, true, "disable_web_applet", Category::Ui};

    // Discord RPC
    Setting<bool> enable_discord_presence{linkage, true, "enable_discord_presence", Category::Ui};

    // logging
    Setting<bool> show_console{linkage, false, "showConsole", Category::Ui};

    // Screenshots
    Setting<bool> enable_screenshot_save_as{linkage, true, "enable_screenshot_save_as",
                                            Category::Screenshots};
    Setting<u32> screenshot_height{linkage, 0, "screenshot_height", Category::Screenshots};

    std::string roms_path;
    std::string game_dir_deprecated;
    bool game_dir_deprecated_deepscan;
    QVector<GameDir> game_dirs;
    QStringList recent_files;
    Setting<std::string> language{linkage, {}, "language", Category::Paths};

    QString theme;
    DarkModeState dark_mode_state;

    // Shortcut name <Shortcut, context>
    std::vector<Shortcut> shortcuts;

    Setting<u32> callout_flags{linkage, 0, "calloutFlags", Category::Ui};

    // multiplayer settings
    Setting<std::string> multiplayer_nickname{linkage, {}, "nickname", Category::Multiplayer};
    Setting<std::string> multiplayer_filter_text{linkage, {}, "filter_text", Category::Multiplayer};
    Setting<bool> multiplayer_filter_games_owned{linkage, false, "filter_games_owned",
                                                 Category::Multiplayer};
    Setting<bool> multiplayer_filter_hide_empty{linkage, false, "filter_games_hide_empty",
                                                Category::Multiplayer};
    Setting<bool> multiplayer_filter_hide_full{linkage, false, "filter_games_hide_full",
                                               Category::Multiplayer};
    Setting<std::string> multiplayer_ip{linkage, {}, "ip", Category::Multiplayer};
    Setting<u16, true> multiplayer_port{linkage,    24872,  0,
                                        UINT16_MAX, "port", Category::Multiplayer};
    Setting<std::string> multiplayer_room_nickname{
        linkage, {}, "room_nickname", Category::Multiplayer};
    Setting<std::string> multiplayer_room_name{linkage, {}, "room_name", Category::Multiplayer};
    Setting<u8, true> multiplayer_max_player{linkage, 8, 0, 8, "max_player", Category::Multiplayer};
    Setting<u16, true> multiplayer_room_port{linkage,    24872,       0,
                                             UINT16_MAX, "room_port", Category::Multiplayer};
    Setting<u8, true> multiplayer_host_type{linkage, 0, 0, 1, "host_type", Category::Multiplayer};
    Setting<unsigned long long> multiplayer_game_id{linkage, {}, "game_id", Category::Multiplayer};
    Setting<std::string> multiplayer_room_description{
        linkage, {}, "room_description", Category::Multiplayer};
    std::pair<std::vector<std::string>, std::vector<std::string>> multiplayer_ban_list;

    // Game List
    Setting<bool> show_add_ons{linkage, true, "show_add_ons", Category::UiGameList};
    Setting<u32> game_icon_size{linkage, 64, "game_icon_size", Category::UiGameList};
    Setting<u32> folder_icon_size{linkage, 48, "folder_icon_size", Category::UiGameList};
    Setting<u8> row_1_text_id{linkage, 3, "row_1_text_id", Category::UiGameList};
    Setting<u8> row_2_text_id{linkage, 2, "row_2_text_id", Category::UiGameList};
    std::atomic_bool is_game_list_reload_pending{false};
    Setting<bool> cache_game_list{linkage, true, "cache_game_list", Category::UiGameList};
    Setting<bool> favorites_expanded{linkage, true, "favorites_expanded", Category::UiGameList};
    Setting<bool> show_folders_in_list{linkage, true, "show_folders_in_list", Category::UiGameList};
    QVector<u64> favorited_ids;

    // Compatibility List
    Setting<bool> show_compat{linkage, false, "show_compat", Category::UiGameList};

    // Size & File Types Column
    Setting<bool> show_size{linkage, true, "show_size", Category::UiGameList};
    Setting<bool> show_types{linkage, true, "show_types", Category::UiGameList};

    // Play time
    Setting<bool> show_play_time{linkage, true, "show_play_time", Category::UiGameList};

    bool configuration_applied;
    bool reset_to_defaults;
    bool shortcut_already_warned{false};
};

extern Values values;

u32 CalculateWidth(u32 height, Settings::AspectRatio ratio);

void SaveWindowState();
void RestoreWindowState(std::unique_ptr<QtConfig>& qtConfig);

// This shouldn't have anything except static initializers (no functions). So
// QKeySequence(...).toString() is NOT ALLOWED HERE.
// This must be in alphabetical order according to action name as it must have the same order as
// UISetting::values.shortcuts, which is alphabetically ordered.
// clang-format off
const std::array<Shortcut, 28> default_hotkeys{{
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Audio Mute/Unmute")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+M"),  std::string("Home+Dpad_Right"), Qt::WindowShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Audio Volume Down")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("-"),       std::string("Home+Dpad_Down"), Qt::ApplicationShortcut, true}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Audio Volume Up")).toStdString(),          QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("="),       std::string("Home+Dpad_Up"), Qt::ApplicationShortcut, true}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Capture Screenshot")).toStdString(),       QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+P"),  std::string("Screenshot"), Qt::WidgetWithChildrenShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Change Adapting Filter")).toStdString(),   QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F8"),      std::string("Home+L"), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Change Docked Mode")).toStdString(),       QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F10"),     std::string("Home+X"), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Change GPU Accuracy")).toStdString(),      QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F9"),      std::string("Home+R"), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Continue/Pause Emulation")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F4"),      std::string("Home+Plus"), Qt::WindowShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Exit Fullscreen")).toStdString(),          QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Esc"),     std::string(""), Qt::WindowShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Exit suyu")).toStdString(),                QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+Q"),  std::string("Home+Minus"), Qt::WindowShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Fullscreen")).toStdString(),               QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F11"),     std::string("Home+B"), Qt::WindowShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Load File")).toStdString(),                QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+O"),  std::string(""), Qt::WidgetWithChildrenShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Load/Remove Amiibo")).toStdString(),       QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F2"),      std::string("Home+A"), Qt::WidgetWithChildrenShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Browse Public Game Lobby")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+B"),  std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Create Room")).toStdString(),  QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+N"),  std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Direct Connect to Room")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+C"),  std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Leave Room")).toStdString(),   QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+L"),  std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Show Current Room")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+R"),  std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Restart Emulation")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F6"),      std::string("R+Plus+Minus"), Qt::WindowShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Stop Emulation")).toStdString(),           QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F5"),      std::string("L+Plus+Minus"), Qt::WindowShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "TAS Record")).toStdString(),               QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F7"), std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "TAS Reset")).toStdString(),                QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F6"), std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "TAS Start/Stop")).toStdString(),           QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F5"), std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Filter Bar")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F"),  std::string(""), Qt::WindowShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Framerate Limit")).toStdString(),   QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+U"),  std::string("Home+Y"), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Mouse Panning")).toStdString(),     QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F9"), std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Renderdoc Capture")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string(""),        std::string(""), Qt::ApplicationShortcut, false}},
    {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Status Bar")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+S"),  std::string(""), Qt::WindowShortcut, false}},
}};
// clang-format on

} // namespace UISettings

Q_DECLARE_METATYPE(UISettings::GameDir*);

// These metatype declarations cannot be in common/settings.h because core is devoid of QT
Q_DECLARE_METATYPE(Settings::CpuAccuracy);
Q_DECLARE_METATYPE(Settings::GpuAccuracy);
Q_DECLARE_METATYPE(Settings::FullscreenMode);
Q_DECLARE_METATYPE(Settings::NvdecEmulation);
Q_DECLARE_METATYPE(Settings::ResolutionSetup);
Q_DECLARE_METATYPE(Settings::ScalingFilter);
Q_DECLARE_METATYPE(Settings::AntiAliasing);
Q_DECLARE_METATYPE(Settings::RendererBackend);
Q_DECLARE_METATYPE(Settings::ShaderBackend);
Q_DECLARE_METATYPE(Settings::AstcRecompression);
Q_DECLARE_METATYPE(Settings::AstcDecodeMode);
Q_DECLARE_METATYPE(Settings::DarkModeState);
