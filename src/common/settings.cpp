// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <version>
#include "common/settings_enums.h"
#if __cpp_lib_chrono >= 201907L
#include <chrono>
#include <exception>
#include <stdexcept>
#endif
#include <compare>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string_view>
#include <type_traits>
#include <fmt/core.h>

#include "common/assert.h"
#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/time_zone.h"

namespace Settings {

// Clang 14 and earlier have errors when explicitly instantiating these classes
#ifndef CANNOT_EXPLICITLY_INSTANTIATE
#define SETTING(TYPE, RANGED) template class Setting<TYPE, RANGED>
#define SWITCHABLE(TYPE, RANGED) template class SwitchableSetting<TYPE, RANGED>

SETTING(AppletMode, false);
SETTING(AudioEngine, false);
SETTING(bool, false);
SETTING(int, false);
SETTING(std::string, false);
SETTING(u16, false);
SWITCHABLE(AnisotropyMode, true);
SWITCHABLE(AntiAliasing, false);
SWITCHABLE(AspectRatio, true);
SWITCHABLE(AstcDecodeMode, true);
SWITCHABLE(AstcRecompression, true);
SWITCHABLE(AudioMode, true);
SWITCHABLE(CpuBackend, true);
SWITCHABLE(CpuAccuracy, true);
SWITCHABLE(FullscreenMode, true);
SWITCHABLE(GpuAccuracy, true);
SWITCHABLE(Language, true);
SWITCHABLE(MemoryLayout, true);
SWITCHABLE(NvdecEmulation, false);
SWITCHABLE(Region, true);
SWITCHABLE(RendererBackend, true);
SWITCHABLE(ScalingFilter, false);
SWITCHABLE(ShaderBackend, true);
SWITCHABLE(TimeZone, true);
SETTING(VSyncMode, true);
SWITCHABLE(bool, false);
SWITCHABLE(int, false);
SWITCHABLE(int, true);
SWITCHABLE(s64, false);
SWITCHABLE(u16, true);
SWITCHABLE(u32, false);
SWITCHABLE(u8, false);
SWITCHABLE(u8, true);

// Used in UISettings
// TODO see if we can move this to uisettings.cpp
SWITCHABLE(ConfirmStop, true);
SWITCHABLE(DarkModeState, true);

#undef SETTING
#undef SWITCHABLE
#endif

Values values;

std::string GetTimeZoneString(TimeZone time_zone) {
    const auto time_zone_index = static_cast<std::size_t>(time_zone);
    ASSERT(time_zone_index < Common::TimeZone::GetTimeZoneStrings().size());

    std::string location_name;
    if (time_zone_index == 0) { // Auto
#if __cpp_lib_chrono >= 201907L && !defined(MINGW)
        // Disabled for MinGW -- tzdb always returns Etc/UTC
        try {
            const struct std::chrono::tzdb& time_zone_data = std::chrono::get_tzdb();
            const std::chrono::time_zone* current_zone = time_zone_data.current_zone();
            std::string_view current_zone_name = current_zone->name();
            location_name = current_zone_name;
        } catch (std::runtime_error& runtime_error) {
            // VCRUNTIME will throw a runtime_error if the operating system's selected time zone
            // cannot be found
            location_name = Common::TimeZone::FindSystemTimeZone();
            LOG_WARNING(Common,
                        "Error occurred when trying to determine system time zone:\n{}\nFalling "
                        "back to hour offset \"{}\"",
                        runtime_error.what(), location_name);
        }
#else
        location_name = Common::TimeZone::FindSystemTimeZone();
#endif
    } else {
        location_name = Common::TimeZone::GetTimeZoneStrings()[time_zone_index];
    }
    return location_name;
}

void LogSettings() {
    const auto log_setting = [](std::string_view name, const auto& value) {
        LOG_INFO(Config, "{}: {}", name, value);
    };

    const auto log_path = [](std::string_view name, const std::filesystem::path& path) {
        LOG_INFO(Config, "{}: {}", name, Common::FS::PathToUTF8String(path));
    };

    LOG_INFO(Config, "suyu Configuration:");
    for (auto& [category, settings] : values.linkage.by_category) {
        for (const auto& setting : settings) {
            if (setting->Id() == values.suyu_token.Id()) {
                // Hide the token secret, for security reasons.
                continue;
            }

            const auto name = fmt::format(
                "{:c}{:c} {}.{}", setting->ToString() == setting->DefaultToString() ? '-' : 'M',
                setting->UsingGlobal() ? '-' : 'C', TranslateCategory(category),
                setting->GetLabel());

            log_setting(name, setting->Canonicalize());
        }
    }
    log_path("DataStorage_CacheDir", Common::FS::GetSuyuPath(Common::FS::SuyuPath::CacheDir));
    log_path("DataStorage_ConfigDir", Common::FS::GetSuyuPath(Common::FS::SuyuPath::ConfigDir));
    log_path("DataStorage_LoadDir", Common::FS::GetSuyuPath(Common::FS::SuyuPath::LoadDir));
    log_path("DataStorage_NANDDir", Common::FS::GetSuyuPath(Common::FS::SuyuPath::NANDDir));
    log_path("DataStorage_SDMCDir", Common::FS::GetSuyuPath(Common::FS::SuyuPath::SDMCDir));
}

void UpdateGPUAccuracy() {
    values.current_gpu_accuracy = values.gpu_accuracy.GetValue();
}

bool IsGPULevelExtreme() {
    return values.current_gpu_accuracy == GpuAccuracy::Extreme;
}

bool IsGPULevelHigh() {
    return values.current_gpu_accuracy == GpuAccuracy::Extreme ||
           values.current_gpu_accuracy == GpuAccuracy::High;
}

bool IsFastmemEnabled() {
    if (values.cpu_debug_mode) {
        return static_cast<bool>(values.cpuopt_fastmem);
    }
    return true;
}

static bool is_nce_enabled = false;

void SetNceEnabled(bool is_39bit) {
    const bool is_nce_selected = values.cpu_backend.GetValue() == CpuBackend::Nce;
    if (is_nce_selected && !IsFastmemEnabled()) {
        LOG_WARNING(Common, "Fastmem is required to natively execute code in a performant manner, "
                            "falling back to Dynarmic");
    }
    if (is_nce_selected && !is_39bit) {
        LOG_WARNING(
            Common,
            "Program does not utilize 39-bit address space, unable to natively execute code");
    }
    is_nce_enabled = IsFastmemEnabled() && is_nce_selected && is_39bit;
}

bool IsNceEnabled() {
    return is_nce_enabled;
}

bool IsDockedMode() {
    return values.use_docked_mode.GetValue() == Settings::ConsoleMode::Docked;
}

float Volume() {
    if (values.audio_muted) {
        return 0.0f;
    }
    return values.volume.GetValue() / static_cast<f32>(values.volume.GetDefault());
}

const char* TranslateCategory(Category category) {
    switch (category) {
    case Category::Android:
        return "Android";
    case Category::Audio:
        return "Audio";
    case Category::Core:
        return "Core";
    case Category::Cpu:
    case Category::CpuDebug:
    case Category::CpuUnsafe:
        return "Cpu";
    case Category::Overlay:
        return "Overlay";
    case Category::Renderer:
    case Category::RendererAdvanced:
    case Category::RendererDebug:
        return "Renderer";
    case Category::System:
    case Category::SystemAudio:
        return "System";
    case Category::DataStorage:
        return "Data Storage";
    case Category::Debugging:
    case Category::DebuggingGraphics:
        return "Debugging";
    case Category::GpuDriver:
        return "GpuDriver";
    case Category::LibraryApplet:
        return "LibraryApplet";
    case Category::Miscellaneous:
        return "Miscellaneous";
    case Category::Network:
        return "Network";
    case Category::WebService:
        return "WebService";
    case Category::AddOns:
        return "DisabledAddOns";
    case Category::Controls:
        return "Controls";
    case Category::Ui:
    case Category::UiGeneral:
        return "UI";
    case Category::UiAudio:
        return "UiAudio";
    case Category::UiLayout:
        return "UILayout";
    case Category::UiGameList:
        return "UIGameList";
    case Category::Screenshots:
        return "Screenshots";
    case Category::Shortcuts:
        return "Shortcuts";
    case Category::Multiplayer:
        return "Multiplayer";
    case Category::Services:
        return "Services";
    case Category::Paths:
        return "Paths";
    case Category::Linux:
        return "Linux";
    case Category::MaxEnum:
        break;
    }
    return "Miscellaneous";
}

void TranslateResolutionInfo(ResolutionSetup setup, ResolutionScalingInfo& info) {
    info.downscale = false;
    switch (setup) {
    case ResolutionSetup::Res1_2X:
        info.up_scale = 1;
        info.down_shift = 1;
        info.downscale = true;
        break;
    case ResolutionSetup::Res3_4X:
        info.up_scale = 3;
        info.down_shift = 2;
        info.downscale = true;
        break;
    case ResolutionSetup::Res1X:
        info.up_scale = 1;
        info.down_shift = 0;
        break;
    case ResolutionSetup::Res3_2X:
        info.up_scale = 3;
        info.down_shift = 1;
        break;
    case ResolutionSetup::Res2X:
        info.up_scale = 2;
        info.down_shift = 0;
        break;
    case ResolutionSetup::Res3X:
        info.up_scale = 3;
        info.down_shift = 0;
        break;
    case ResolutionSetup::Res4X:
        info.up_scale = 4;
        info.down_shift = 0;
        break;
    case ResolutionSetup::Res5X:
        info.up_scale = 5;
        info.down_shift = 0;
        break;
    case ResolutionSetup::Res6X:
        info.up_scale = 6;
        info.down_shift = 0;
        break;
    case ResolutionSetup::Res7X:
        info.up_scale = 7;
        info.down_shift = 0;
        break;
    case ResolutionSetup::Res8X:
        info.up_scale = 8;
        info.down_shift = 0;
        break;
    default:
        ASSERT(false);
        info.up_scale = 1;
        info.down_shift = 0;
        break;
    }
    info.up_factor = static_cast<f32>(info.up_scale) / (1U << info.down_shift);
    info.down_factor = static_cast<f32>(1U << info.down_shift) / info.up_scale;
    info.active = info.up_scale != 1 || info.down_shift != 0;
}

void UpdateRescalingInfo() {
    const auto setup = values.resolution_setup.GetValue();
    auto& info = values.resolution_info;
    TranslateResolutionInfo(setup, info);
}

void RestoreGlobalState(bool is_powered_on) {
    // If a game is running, DO NOT restore the global settings state
    if (is_powered_on) {
        return;
    }

    for (const auto& reset : values.linkage.restore_functions) {
        reset();
    }
}

static bool configuring_global = true;

bool IsConfiguringGlobal() {
    return configuring_global;
}

void SetConfiguringGlobal(bool is_global) {
    configuring_global = is_global;
}

} // namespace Settings
