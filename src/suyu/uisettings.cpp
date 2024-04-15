// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QSettings>
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "suyu/uisettings.h"

#ifndef CANNOT_EXPLICITLY_INSTANTIATE
namespace Settings {
template class Setting<bool>;
template class Setting<std::string>;
template class Setting<u16, true>;
template class Setting<u32>;
template class Setting<u8, true>;
template class Setting<u8>;
template class Setting<unsigned long long>;
} // namespace Settings
#endif

namespace FS = Common::FS;

namespace UISettings {

const Themes included_themes{{
    {"Default", ":/default"},
    {"Default monochrome", ":/monochrome"},
    {"Mine Shaft", ":/qdarkstyle"},
    {"Mine Shaft monochrome", ":/qdarkstyle_monochrome"},
    {"Midnight Blue", ":/qdarkstyle_midnight_blue"},
    {"Midnight Blue monochrome", ":/qdarkstyle_midnight_blue_monochrome"},
}};

Values values = {};

u32 CalculateWidth(u32 height, Settings::AspectRatio ratio) {
    switch (ratio) {
    case Settings::AspectRatio::R4_3:
        return height * 4 / 3;
    case Settings::AspectRatio::R21_9:
        return height * 21 / 9;
    case Settings::AspectRatio::R16_10:
        return height * 16 / 10;
    case Settings::AspectRatio::R32_9:
        return height * 32 / 9;
    case Settings::AspectRatio::R16_9:
    case Settings::AspectRatio::Stretch:
        // TODO: Move this function wherever appropriate to implement Stretched aspect
        break;
    }
    return height * 16 / 9;
}

void SaveWindowState() {
    const auto window_state_config_loc =
        FS::PathToUTF8String(FS::GetSuyuPath(FS::SuyuPath::ConfigDir) / "window_state.ini");

    void(FS::CreateParentDir(window_state_config_loc));
    QSettings config(QString::fromStdString(window_state_config_loc), QSettings::IniFormat);

    config.setValue(QStringLiteral("geometry"), values.geometry);
    config.setValue(QStringLiteral("state"), values.state);
    config.setValue(QStringLiteral("geometryRenderWindow"), values.renderwindow_geometry);
    config.setValue(QStringLiteral("gameListHeaderState"), values.gamelist_header_state);
    config.setValue(QStringLiteral("microProfileDialogGeometry"), values.microprofile_geometry);

    config.sync();
}

void RestoreWindowState(std::unique_ptr<QtConfig>& qtConfig) {
    const auto window_state_config_loc =
        FS::PathToUTF8String(FS::GetSuyuPath(FS::SuyuPath::ConfigDir) / "window_state.ini");

    // Migrate window state from old location
    if (!FS::Exists(window_state_config_loc) && qtConfig->Exists("UI", "UILayout\\geometry")) {
        const auto config_loc =
            FS::PathToUTF8String(FS::GetSuyuPath(FS::SuyuPath::ConfigDir) / "qt-config.ini");
        QSettings config(QString::fromStdString(config_loc), QSettings::IniFormat);

        config.beginGroup(QStringLiteral("UI"));
        config.beginGroup(QStringLiteral("UILayout"));
        values.geometry = config.value(QStringLiteral("geometry")).toByteArray();
        values.state = config.value(QStringLiteral("state")).toByteArray();
        values.renderwindow_geometry =
            config.value(QStringLiteral("geometryRenderWindow")).toByteArray();
        values.gamelist_header_state =
            config.value(QStringLiteral("gameListHeaderState")).toByteArray();
        values.microprofile_geometry =
            config.value(QStringLiteral("microProfileDialogGeometry")).toByteArray();
        config.endGroup();
        config.endGroup();
        return;
    }

    void(FS::CreateParentDir(window_state_config_loc));
    const QSettings config(QString::fromStdString(window_state_config_loc), QSettings::IniFormat);

    values.geometry = config.value(QStringLiteral("geometry")).toByteArray();
    values.state = config.value(QStringLiteral("state")).toByteArray();
    values.renderwindow_geometry =
        config.value(QStringLiteral("geometryRenderWindow")).toByteArray();
    values.gamelist_header_state =
        config.value(QStringLiteral("gameListHeaderState")).toByteArray();
    values.microprofile_geometry =
        config.value(QStringLiteral("microProfileDialogGeometry")).toByteArray();
}

} // namespace UISettings
