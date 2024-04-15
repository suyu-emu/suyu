// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>
#include "common/common_types.h"

namespace Settings {

template <typename T>
struct EnumMetadata {
    static std::vector<std::pair<std::string, T>> Canonicalizations();
    static u32 Index();
};

#define PAIR_45(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_46(N, __VA_ARGS__))
#define PAIR_44(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_45(N, __VA_ARGS__))
#define PAIR_43(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_44(N, __VA_ARGS__))
#define PAIR_42(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_43(N, __VA_ARGS__))
#define PAIR_41(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_42(N, __VA_ARGS__))
#define PAIR_40(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_41(N, __VA_ARGS__))
#define PAIR_39(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_40(N, __VA_ARGS__))
#define PAIR_38(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_39(N, __VA_ARGS__))
#define PAIR_37(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_38(N, __VA_ARGS__))
#define PAIR_36(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_37(N, __VA_ARGS__))
#define PAIR_35(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_36(N, __VA_ARGS__))
#define PAIR_34(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_35(N, __VA_ARGS__))
#define PAIR_33(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_34(N, __VA_ARGS__))
#define PAIR_32(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_33(N, __VA_ARGS__))
#define PAIR_31(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_32(N, __VA_ARGS__))
#define PAIR_30(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_31(N, __VA_ARGS__))
#define PAIR_29(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_30(N, __VA_ARGS__))
#define PAIR_28(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_29(N, __VA_ARGS__))
#define PAIR_27(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_28(N, __VA_ARGS__))
#define PAIR_26(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_27(N, __VA_ARGS__))
#define PAIR_25(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_26(N, __VA_ARGS__))
#define PAIR_24(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_25(N, __VA_ARGS__))
#define PAIR_23(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_24(N, __VA_ARGS__))
#define PAIR_22(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_23(N, __VA_ARGS__))
#define PAIR_21(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_22(N, __VA_ARGS__))
#define PAIR_20(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_21(N, __VA_ARGS__))
#define PAIR_19(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_20(N, __VA_ARGS__))
#define PAIR_18(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_19(N, __VA_ARGS__))
#define PAIR_17(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_18(N, __VA_ARGS__))
#define PAIR_16(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_17(N, __VA_ARGS__))
#define PAIR_15(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_16(N, __VA_ARGS__))
#define PAIR_14(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_15(N, __VA_ARGS__))
#define PAIR_13(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_14(N, __VA_ARGS__))
#define PAIR_12(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_13(N, __VA_ARGS__))
#define PAIR_11(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_12(N, __VA_ARGS__))
#define PAIR_10(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_11(N, __VA_ARGS__))
#define PAIR_9(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_10(N, __VA_ARGS__))
#define PAIR_8(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_9(N, __VA_ARGS__))
#define PAIR_7(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_8(N, __VA_ARGS__))
#define PAIR_6(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_7(N, __VA_ARGS__))
#define PAIR_5(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_6(N, __VA_ARGS__))
#define PAIR_4(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_5(N, __VA_ARGS__))
#define PAIR_3(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_4(N, __VA_ARGS__))
#define PAIR_2(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_3(N, __VA_ARGS__))
#define PAIR_1(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_2(N, __VA_ARGS__))
#define PAIR(N, X, ...) {#X, N::X} __VA_OPT__(, PAIR_1(N, __VA_ARGS__))

#define ENUM(NAME, ...)                                                                            \
    enum class NAME : u32 { __VA_ARGS__ };                                                         \
    template <>                                                                                    \
    inline std::vector<std::pair<std::string, NAME>> EnumMetadata<NAME>::Canonicalizations() {     \
        return {PAIR(NAME, __VA_ARGS__)};                                                          \
    }                                                                                              \
    template <>                                                                                    \
    inline u32 EnumMetadata<NAME>::Index() {                                                       \
        return __COUNTER__;                                                                        \
    }

// AudioEngine must be specified discretely due to having existing but slightly different
// canonicalizations
// TODO (lat9nq): Remove explicit definition of AudioEngine/sink_id
enum class AudioEngine : u32 {
    Auto,
    Cubeb,
    Sdl2,
    Null,
    Oboe,
};

template <>
inline std::vector<std::pair<std::string, AudioEngine>>
EnumMetadata<AudioEngine>::Canonicalizations() {
    return {
        {"auto", AudioEngine::Auto}, {"cubeb", AudioEngine::Cubeb}, {"sdl2", AudioEngine::Sdl2},
        {"null", AudioEngine::Null}, {"oboe", AudioEngine::Oboe},
    };
}

template <>
inline u32 EnumMetadata<AudioEngine>::Index() {
    // This is just a sufficiently large number that is more than the number of other enums declared
    // here
    return 100;
}

ENUM(AudioMode, Mono, Stereo, Surround);

ENUM(Language, Japanese, EnglishAmerican, French, German, Italian, Spanish, Chinese, Korean, Dutch,
     Portuguese, Russian, Taiwanese, EnglishBritish, FrenchCanadian, SpanishLatin,
     ChineseSimplified, ChineseTraditional, PortugueseBrazilian);

ENUM(Region, Japan, Usa, Europe, Australia, China, Korea, Taiwan);

ENUM(TimeZone, Auto, Default, Cet, Cst6Cdt, Cuba, Eet, Egypt, Eire, Est, Est5Edt, Gb, GbEire, Gmt,
     GmtPlusZero, GmtMinusZero, GmtZero, Greenwich, Hongkong, Hst, Iceland, Iran, Israel, Jamaica,
     Japan, Kwajalein, Libya, Met, Mst, Mst7Mdt, Navajo, Nz, NzChat, Poland, Portugal, Prc, Pst8Pdt,
     Roc, Rok, Singapore, Turkey, Uct, Universal, Utc, WSu, Wet, Zulu);

ENUM(AnisotropyMode, Automatic, Default, X2, X4, X8, X16);

ENUM(AstcDecodeMode, Cpu, Gpu, CpuAsynchronous);

ENUM(AstcRecompression, Uncompressed, Bc1, Bc3);

ENUM(VSyncMode, Immediate, Mailbox, Fifo, FifoRelaxed);

ENUM(VramUsageMode, Conservative, Aggressive);

ENUM(RendererBackend, OpenGL, Vulkan, Null);

ENUM(ShaderBackend, Glsl, Glasm, SpirV);

ENUM(GpuAccuracy, Normal, High, Extreme);

ENUM(CpuBackend, Dynarmic, Nce);

ENUM(CpuAccuracy, Auto, Accurate, Unsafe, Paranoid);

ENUM(MemoryLayout, Memory_4Gb, Memory_6Gb, Memory_8Gb);

ENUM(ConfirmStop, Ask_Always, Ask_Based_On_Game, Ask_Never);

ENUM(FullscreenMode, Borderless, Exclusive);

ENUM(NvdecEmulation, Off, Cpu, Gpu);

ENUM(ResolutionSetup, Res1_2X, Res3_4X, Res1X, Res3_2X, Res2X, Res3X, Res4X, Res5X, Res6X, Res7X,
     Res8X);

ENUM(ScalingFilter, NearestNeighbor, Bilinear, Bicubic, Gaussian, ScaleForce, Fsr, MaxEnum);

ENUM(AntiAliasing, None, Fxaa, Smaa, MaxEnum);

ENUM(AspectRatio, R16_9, R4_3, R21_9, R16_10, R32_9, Stretch);

ENUM(ConsoleMode, Handheld, Docked);

ENUM(AppletMode, HLE, LLE);

ENUM(DarkModeState, Off, On, Auto);

template <typename Type>
inline std::string CanonicalizeEnum(Type id) {
    const auto group = EnumMetadata<Type>::Canonicalizations();
    for (auto& [name, value] : group) {
        if (value == id) {
            return name;
        }
    }
    return "unknown";
}

template <typename Type>
inline Type ToEnum(const std::string& canonicalization) {
    const auto group = EnumMetadata<Type>::Canonicalizations();
    for (auto& [name, value] : group) {
        if (name == canonicalization) {
            return value;
        }
    }
    return {};
}
} // namespace Settings

#undef ENUM
#undef PAIR
#undef PAIR_1
#undef PAIR_2
#undef PAIR_3
#undef PAIR_4
#undef PAIR_5
#undef PAIR_6
#undef PAIR_7
#undef PAIR_8
#undef PAIR_9
#undef PAIR_10
#undef PAIR_12
#undef PAIR_13
#undef PAIR_14
#undef PAIR_15
#undef PAIR_16
#undef PAIR_17
#undef PAIR_18
#undef PAIR_19
#undef PAIR_20
#undef PAIR_22
#undef PAIR_23
#undef PAIR_24
#undef PAIR_25
#undef PAIR_26
#undef PAIR_27
#undef PAIR_28
#undef PAIR_29
#undef PAIR_30
#undef PAIR_32
#undef PAIR_33
#undef PAIR_34
#undef PAIR_35
#undef PAIR_36
#undef PAIR_37
#undef PAIR_38
#undef PAIR_39
#undef PAIR_40
#undef PAIR_42
#undef PAIR_43
#undef PAIR_44
#undef PAIR_45
