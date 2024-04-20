// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>

#include "common/common_types.h"
#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/polyfill_ranges.h"
#include "common/stb.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/acc/acc.h"
#include "core/hle/service/acc/acc_aa.h"
#include "core/hle/service/acc/acc_su.h"
#include "core/hle/service/acc/acc_u0.h"
#include "core/hle/service/acc/acc_u1.h"
#include "core/hle/service/acc/async_context.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/server_manager.h"
#include "core/loader/loader.h"

namespace Service::Account {

// Thumbnails are hard coded to be at least this size
constexpr std::size_t THUMBNAIL_SIZE = 0x24000;

static std::filesystem::path GetImagePath(const Common::UUID& uuid) {
    return Common::FS::GetSuyuPath(Common::FS::SuyuPath::NANDDir) /
           fmt::format("system/save/8000000000000010/su/avators/{}.jpg", uuid.FormattedString());
}

static void JPGToMemory(void* context, void* data, int len) {
    std::vector<u8>* jpg_image = static_cast<std::vector<u8>*>(context);
    unsigned char* jpg = static_cast<unsigned char*>(data);
    jpg_image->insert(jpg_image->end(), jpg, jpg + len);
}

static void SanitizeJPEGImageSize(std::vector<u8>& image) {
    constexpr std::size_t max_jpeg_image_size = 0x20000;
    constexpr int profile_dimensions = 256;
    int original_width, original_height, color_channels;

    const auto plain_image =
        stbi_load_from_memory(image.data(), static_cast<int>(image.size()), &original_width,
                              &original_height, &color_channels, STBI_rgb);

    // Resize image to match 256*256
    if (original_width != profile_dimensions || original_height != profile_dimensions) {
        // Use vector instead of array to avoid overflowing the stack
        std::vector<u8> out_image(profile_dimensions * profile_dimensions * STBI_rgb);
        stbir_resize_uint8_srgb(plain_image, original_width, original_height, 0, out_image.data(),
                                profile_dimensions, profile_dimensions, 0, STBI_rgb, 0,
                                STBIR_FILTER_BOX);
        image.clear();
        if (!stbi_write_jpg_to_func(JPGToMemory, &image, profile_dimensions, profile_dimensions,
                                    STBI_rgb, out_image.data(), 0)) {
            LOG_ERROR(Service_ACC, "Failed to resize the user provided image.");
        }
    }

    image.resize(std::min(image.size(), max_jpeg_image_size));
}

class IManagerForSystemService final : public ServiceFramework<IManagerForSystemService> {
public:
    explicit IManagerForSystemService(Core::System& system_, Common::UUID uuid)
        : ServiceFramework{system_, "IManagerForSystemService"}, account_id{uuid} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IManagerForSystemService::CheckAvailability>, "CheckAvailability"},
            {1, D<&IManagerForSystemService::GetAccountId>, "GetAccountId"},
            {2, nullptr, "EnsureIdTokenCacheAsync"},
            {3, nullptr, "LoadIdTokenCache"},
            {100, nullptr, "SetSystemProgramIdentification"},
            {101, nullptr, "RefreshNotificationTokenAsync"}, // 7.0.0+
            {110, nullptr, "GetServiceEntryRequirementCache"}, // 4.0.0+
            {111, nullptr, "InvalidateServiceEntryRequirementCache"}, // 4.0.0+
            {112, nullptr, "InvalidateTokenCache"}, // 4.0.0 - 6.2.0
            {113, nullptr, "GetServiceEntryRequirementCacheForOnlinePlay"}, // 6.1.0+
            {120, nullptr, "GetNintendoAccountId"},
            {121, nullptr, "CalculateNintendoAccountAuthenticationFingerprint"}, // 9.0.0+
            {130, nullptr, "GetNintendoAccountUserResourceCache"},
            {131, nullptr, "RefreshNintendoAccountUserResourceCacheAsync"},
            {132, nullptr, "RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed"},
            {133, nullptr, "GetNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {134, nullptr, "RefreshNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {135, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsyncIfSecondsElapsed"}, // 9.0.0+
            {140, nullptr, "GetNetworkServiceLicenseCache"}, // 5.0.0+
            {141, nullptr, "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            {142, nullptr, "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            {150, nullptr, "CreateAuthorizationRequest"},
            {160, nullptr, "RequiresUpdateNetworkServiceAccountIdTokenCache"},
            {161, nullptr, "RequireReauthenticationOfNetworkServiceAccount"},
            {180, nullptr, "GetRequestForNintendoAccountReauthentication"}, // 18.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CheckAvailability() {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result GetAccountId(Out<u64> out_account_id) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        *out_account_id = account_id.Hash();
        R_SUCCEED();
    }

    Common::UUID account_id;
};

// 3.0.0+
class IFloatingRegistrationRequest final : public ServiceFramework<IFloatingRegistrationRequest> {
public:
    explicit IFloatingRegistrationRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IFloatingRegistrationRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSessionId"},
            {12, nullptr, "GetAccountId"},
            {13, nullptr, "GetLinkedNintendoAccountId"},
            {14, nullptr, "GetNickname"},
            {15, nullptr, "GetProfileImage"},
            {16, nullptr, "GetProfileLargeImage"}, // 18.0.0+
            {21, nullptr, "LoadIdTokenCache"},
            {100, nullptr, "RegisterUser"}, // [1.0.0-3.0.2] RegisterAsync
            {101, nullptr, "RegisterUserWithUid"}, // [1.0.0-3.0.2] RegisterWithUidAsync
            {102, nullptr, "RegisterNetworkServiceAccountAsync"}, // 4.0.0+
            {103, nullptr, "RegisterNetworkServiceAccountWithUidAsync"}, // 4.0.0+
            {110, nullptr, "SetSystemProgramIdentification"},
            {111, nullptr, "EnsureIdTokenCacheAsync"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IAdministrator final : public ServiceFramework<IAdministrator> {
public:
    explicit IAdministrator(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAdministrator"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "CheckAvailability"},
            {1, nullptr, "GetAccountId"},
            {2, nullptr, "EnsureIdTokenCacheAsync"},
            {3, nullptr, "LoadIdTokenCache"},
            {100, nullptr, "SetSystemProgramIdentification"},
            {101, nullptr, "RefreshNotificationTokenAsync"}, // 7.0.0+
            {110, nullptr, "GetServiceEntryRequirementCache"}, // 4.0.0+
            {111, nullptr, "InvalidateServiceEntryRequirementCache"}, // 4.0.0+
            {112, nullptr, "InvalidateTokenCache"}, // 4.0.0 - 6.2.0
            {113, nullptr, "GetServiceEntryRequirementCacheForOnlinePlay"}, // 6.1.0+
            {120, nullptr, "GetNintendoAccountId"},
            {121, nullptr, "CalculateNintendoAccountAuthenticationFingerprint"}, // 9.0.0+
            {130, nullptr, "GetNintendoAccountUserResourceCache"},
            {131, nullptr, "RefreshNintendoAccountUserResourceCacheAsync"},
            {132, nullptr, "RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed"},
            {133, nullptr, "GetNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {134, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsync"}, // 9.0.0+
            {135, nullptr, "RefreshNintendoAccountVerificationUrlCacheAsyncIfSecondsElapsed"}, // 9.0.0+
            {140, nullptr, "GetNetworkServiceLicenseCache"}, // 5.0.0+
            {141, nullptr, "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            {142, nullptr, "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            {143, nullptr, "GetNetworkServiceLicenseCacheEx"}, // 15.0.0+
            {150, nullptr, "CreateAuthorizationRequest"},
            {160, nullptr, "RequiresUpdateNetworkServiceAccountIdTokenCache"}, // 16.0.0+
            {161, nullptr, "RequireReauthenticationOfNetworkServiceAccount"}, // 16.0.0+
            {180, nullptr, "GetRequestForNintendoAccountReauthentication"}, // 18.0.0+
            {200, nullptr, "IsRegistered"},
            {201, nullptr, "RegisterAsync"},
            {202, nullptr, "UnregisterAsync"},
            {203, nullptr, "DeleteRegistrationInfoLocally"},
            {220, nullptr, "SynchronizeProfileAsync"},
            {221, nullptr, "UploadProfileAsync"},
            {222, nullptr, "SynchronizaProfileAsyncIfSecondsElapsed"},
            {250, nullptr, "IsLinkedWithNintendoAccount"},
            {251, nullptr, "CreateProcedureToLinkWithNintendoAccount"},
            {252, nullptr, "ResumeProcedureToLinkWithNintendoAccount"},
            {255, nullptr, "CreateProcedureToUpdateLinkageStateOfNintendoAccount"},
            {256, nullptr, "ResumeProcedureToUpdateLinkageStateOfNintendoAccount"},
            {260, nullptr, "CreateProcedureToLinkNnidWithNintendoAccount"}, // 3.0.0+
            {261, nullptr, "ResumeProcedureToLinkNnidWithNintendoAccount"}, // 3.0.0+
            {280, nullptr, "ProxyProcedureToAcquireApplicationAuthorizationForNintendoAccount"},
            {290, nullptr, "GetRequestForNintendoAccountUserResourceView"}, // 8.0.0+
            {300, nullptr, "TryRecoverNintendoAccountUserStateAsync"}, // 6.0.0+
            {400, nullptr, "IsServiceEntryRequirementCacheRefreshRequiredForOnlinePlay"}, // 6.1.0+
            {401, nullptr, "RefreshServiceEntryRequirementCacheForOnlinePlayAsync"}, // 6.1.0+
            {900, nullptr, "GetAuthenticationInfoForWin"}, // 9.0.0+
            {901, nullptr, "ImportAsyncForWin"}, // 9.0.0+
            {997, nullptr, "DebugUnlinkNintendoAccountAsync"},
            {998, nullptr, "DebugSetAvailabilityErrorDetail"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IAuthorizationRequest final : public ServiceFramework<IAuthorizationRequest> {
public:
    explicit IAuthorizationRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAuthorizationRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSessionId"},
            {10, nullptr, "InvokeWithoutInteractionAsync"},
            {19, nullptr, "IsAuthorized"},
            {20, nullptr, "GetAuthorizationCode"},
            {21, nullptr, "GetIdToken"},
            {22, nullptr, "GetState"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IOAuthProcedure final : public ServiceFramework<IOAuthProcedure> {
public:
    explicit IOAuthProcedure(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedure"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "PrepareAsync"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "ApplyResponse"},
            {3, nullptr, "ApplyResponseAsync"},
            {10, nullptr, "Suspend"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

// 3.0.0+
class IOAuthProcedureForExternalNsa final : public ServiceFramework<IOAuthProcedureForExternalNsa> {
public:
    explicit IOAuthProcedureForExternalNsa(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForExternalNsa"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "PrepareAsync"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "ApplyResponse"},
            {3, nullptr, "ApplyResponseAsync"},
            {10, nullptr, "Suspend"},
            {100, nullptr, "GetAccountId"},
            {101, nullptr, "GetLinkedNintendoAccountId"},
            {102, nullptr, "GetNickname"},
            {103, nullptr, "GetProfileImage"},
            {104, nullptr, "GetProfileLargeImage"}, // 18.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IOAuthProcedureForNintendoAccountLinkage final
    : public ServiceFramework<IOAuthProcedureForNintendoAccountLinkage> {
public:
    explicit IOAuthProcedureForNintendoAccountLinkage(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForNintendoAccountLinkage"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "PrepareAsync"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "ApplyResponse"},
            {3, nullptr, "ApplyResponseAsync"},
            {10, nullptr, "Suspend"},
            {100, nullptr, "GetRequestWithTheme"},
            {101, nullptr, "IsNetworkServiceAccountReplaced"},
            {199, nullptr, "GetUrlForIntroductionOfExtraMembership"}, // 2.0.0 - 5.1.0
            {200, nullptr, "ApplyAsyncWithAuthorizedToken"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class INotifier final : public ServiceFramework<INotifier> {
public:
    explicit INotifier(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "INotifier"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSystemEvent"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IProfileCommon : public ServiceFramework<IProfileCommon> {
public:
    explicit IProfileCommon(Core::System& system_, const char* name, bool editor_commands,
                            Common::UUID user_id_, ProfileManager& profile_manager_)
        : ServiceFramework{system_, name}, profile_manager{profile_manager_}, user_id{user_id_} {
        static const FunctionInfo functions[] = {
            {0, &IProfileCommon::Get, "Get"},
            {1, &IProfileCommon::GetBase, "GetBase"},
            {10, &IProfileCommon::GetImageSize, "GetImageSize"},
            {11, &IProfileCommon::LoadImage, "LoadImage"},
            {20, &IProfileCommon::GetImageSize, "GetLargeImageSize"}, // 18.0.0+
            {21, &IProfileCommon::LoadImage, "LoadLargeImage"},       // 18.0.0+
            {30, &IProfileCommon::Unknown, "GetImageId"},             // 18.0.0+
        };

        RegisterHandlers(functions);

        if (editor_commands) {
            static const FunctionInfo editor_functions[] = {
                {100, &IProfileCommon::Store, "Store"},
                {101, &IProfileCommon::StoreWithImage, "StoreWithImage"},
                {110, &IProfileCommon::StoreWithImage, "StoreWithLargeImage"}, // 18.0.0+
            };

            RegisterHandlers(editor_functions);
        }
    }

protected:
    void Get(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());
        ProfileBase profile_base{};
        UserData data{};
        if (profile_manager.GetProfileBaseAndData(user_id, profile_base, data)) {
            ctx.WriteBuffer(data);
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(ResultSuccess);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base and data for user=0x{}",
                      user_id.RawString());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown); // TODO(ogniK): Get actual error code
        }
    }

    void GetBase(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());
        ProfileBase profile_base{};
        if (profile_manager.GetProfileBase(user_id, profile_base)) {
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(ResultSuccess);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base for user=0x{}", user_id.RawString());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown); // TODO(ogniK): Get actual error code
        }
    }

    void LoadImage(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);
        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            ctx.WriteBuffer(Core::Constants::ACCOUNT_BACKUP_JPEG);
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(image.GetSize());

        if (image.Read(buffer) != buffer.size()) {
            LOG_ERROR(Service_ACC, "Failed to read all the bytes in the user provided image.");
        }

        SanitizeJPEGImageSize(buffer);

        ctx.WriteBuffer(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void GetImageSize(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);

        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(image.GetSize());

        if (image.Read(buffer) != buffer.size()) {
            LOG_ERROR(Service_ACC, "Failed to read all the bytes in the user provided image.");
        }

        SanitizeJPEGImageSize(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void Store(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto base = rp.PopRaw<ProfileBase>();

        const auto user_data = ctx.ReadBuffer();

        LOG_DEBUG(Service_ACC, "called, username='{}', timestamp={:016X}, uuid=0x{}",
                  Common::StringFromFixedZeroTerminatedBuffer(
                      reinterpret_cast<const char*>(base.username.data()), base.username.size()),
                  base.timestamp, base.user_uuid.RawString());

        if (user_data.size() < sizeof(UserData)) {
            LOG_ERROR(Service_ACC, "UserData buffer too small!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultInvalidArrayLength);
            return;
        }

        UserData data;
        std::memcpy(&data, user_data.data(), sizeof(UserData));

        if (!profile_manager.SetProfileBaseAndData(user_id, base, data)) {
            LOG_ERROR(Service_ACC, "Failed to update user data and base!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultAccountUpdateFailed);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void StoreWithImage(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto base = rp.PopRaw<ProfileBase>();

        const auto image_data = ctx.ReadBufferA(0);
        const auto user_data = ctx.ReadBufferX(0);

        LOG_INFO(Service_ACC, "called, username='{}', timestamp={:016X}, uuid=0x{}",
                 Common::StringFromFixedZeroTerminatedBuffer(
                     reinterpret_cast<const char*>(base.username.data()), base.username.size()),
                 base.timestamp, base.user_uuid.RawString());

        if (user_data.size() < sizeof(UserData)) {
            LOG_ERROR(Service_ACC, "UserData buffer too small!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultInvalidArrayLength);
            return;
        }

        UserData data;
        std::memcpy(&data, user_data.data(), sizeof(UserData));

        Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Write,
                                 Common::FS::FileType::BinaryFile);

        if (!image.IsOpen() || !image.SetSize(image_data.size()) ||
            image.Write(image_data) != image_data.size() ||
            !profile_manager.SetProfileBaseAndData(user_id, base, data)) {
            LOG_ERROR(Service_ACC, "Failed to update profile data, base, and image!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultAccountUpdateFailed);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

    ProfileManager& profile_manager;
    Common::UUID user_id{}; ///< The user id this profile refers to.
};

class IProfile final : public IProfileCommon {
public:
    explicit IProfile(Core::System& system_, Common::UUID user_id_,
                      ProfileManager& profile_manager_)
        : IProfileCommon{system_, "IProfile", false, user_id_, profile_manager_} {}
};

class IProfileEditor final : public IProfileCommon {
public:
    explicit IProfileEditor(Core::System& system_, Common::UUID user_id_,
                            ProfileManager& profile_manager_)
        : IProfileCommon{system_, "IProfileEditor", true, user_id_, profile_manager_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {30, &IProfileEditor::Unknown, "Unknown"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class ISessionObject final : public ServiceFramework<ISessionObject> {
public:
    explicit ISessionObject(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "ISessionObject"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {999, nullptr, "Dummy"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IGuestLoginRequest final : public ServiceFramework<IGuestLoginRequest> {
public:
    explicit IGuestLoginRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IGuestLoginRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSessionId"},
            {11, nullptr, "Unknown"}, // 1.0.0 - 2.3.0 (the name is blank on Switchbrew)
            {12, nullptr, "GetAccountId"},
            {13, nullptr, "GetLinkedNintendoAccountId"},
            {14, nullptr, "GetNickname"},
            {15, nullptr, "GetProfileImage"},
            {16, nullptr, "GetProfileLargeImage"}, // 18.0.0+
            {21, nullptr, "LoadIdTokenCache"}, // 3.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class EnsureTokenIdCacheAsyncInterface final : public IAsyncContext {
public:
    explicit EnsureTokenIdCacheAsyncInterface(Core::System& system_) : IAsyncContext{system_} {
        MarkComplete();
    }
    ~EnsureTokenIdCacheAsyncInterface() = default;

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

class IManagerForApplication final : public ServiceFramework<IManagerForApplication> {
public:
    explicit IManagerForApplication(Core::System& system_,
                                    const std::shared_ptr<ProfileManager>& profile_manager_)
        : ServiceFramework{system_, "IManagerForApplication"},
          ensure_token_id{std::make_shared<EnsureTokenIdCacheAsyncInterface>(system)},
          profile_manager{profile_manager_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IManagerForApplication::CheckAvailability, "CheckAvailability"},
            {1, &IManagerForApplication::GetAccountId, "GetAccountId"},
            {2, &IManagerForApplication::EnsureIdTokenCacheAsync, "EnsureIdTokenCacheAsync"},
            {3, &IManagerForApplication::LoadIdTokenCache, "LoadIdTokenCache"},
            {130, &IManagerForApplication::GetNintendoAccountUserResourceCacheForApplication, "GetNintendoAccountUserResourceCacheForApplication"},
            {150, nullptr, "CreateAuthorizationRequest"},
            {160, &IManagerForApplication::StoreOpenContext, "StoreOpenContext"},
            {170, nullptr, "LoadNetworkServiceLicenseKindAsync"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CheckAvailability(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(false); // TODO: Check when this is supposed to return true and when not
    }

    void GetAccountId(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(profile_manager->GetLastOpenedUser().Hash());
    }

    void EnsureIdTokenCacheAsync(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface(ensure_token_id);
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        ensure_token_id->LoadIdTokenCache(ctx);
    }

    void GetNintendoAccountUserResourceCacheForApplication(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> nas_user_base_for_application(0x68);
        ctx.WriteBuffer(nas_user_base_for_application, 0);

        if (ctx.CanWriteBuffer(1)) {
            std::vector<u8> unknown_out_buffer(ctx.GetWriteBufferSize(1));
            ctx.WriteBuffer(unknown_out_buffer, 1);
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(profile_manager->GetLastOpenedUser().Hash());
    }

    void StoreOpenContext(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        profile_manager->StoreOpenedUsers();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    std::shared_ptr<EnsureTokenIdCacheAsyncInterface> ensure_token_id{};
    std::shared_ptr<ProfileManager> profile_manager;
};

// 6.0.0+
class IAsyncNetworkServiceLicenseKindContext final
    : public ServiceFramework<IAsyncNetworkServiceLicenseKindContext> {
public:
    explicit IAsyncNetworkServiceLicenseKindContext(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAsyncNetworkServiceLicenseKindContext"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSystemEvent"},
            {1, nullptr, "Cancel"},
            {2, nullptr, "HasDone"},
            {3, nullptr, "GetResult"},
            {4, nullptr, "GetNetworkServiceLicenseKind"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

// 8.0.0+
class IOAuthProcedureForUserRegistration final
    : public ServiceFramework<IOAuthProcedureForUserRegistration> {
public:
    explicit IOAuthProcedureForUserRegistration(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForUserRegistration"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "PrepareAsync"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "ApplyResponse"},
            {3, nullptr, "ApplyResponseAsync"},
            {10, nullptr, "Suspend"},
            {100, nullptr, "GetAccountId"},
            {101, nullptr, "GetLinkedNintendoAccountId"},
            {102, nullptr, "GetNickname"},
            {103, nullptr, "GetProfileImage"},
            {104, nullptr, "GetProfileLargeImage"}, // 18.0.0+
            {110, nullptr, "RegisterUserAsync"},
            {111, nullptr, "GetUid"},
            {200, nullptr, "ApplyResponseForUserCreationAsync"}, // 17.0.0+
            {205, nullptr, "SuspendAfterApplyResponse"}, // 17.0.0+
            {210, nullptr, "IsProfileAvailable"}, // 17.0.0+
            {220, nullptr, "RegisterUserAsyncWithoutProfile"}, // 17.0.0+
            {221, nullptr, "RegisterUserWithProfileAsync"}, // 17.0.0+
            {230, nullptr, "RegisterUserWithLargeImageProfileAsync"}, // 18.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class DAUTH_O final : public ServiceFramework<DAUTH_O> {
public:
    explicit DAUTH_O(Core::System& system_, Common::UUID) : ServiceFramework{system_, "dauth:o"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "EnsureAuthenticationTokenCacheAsync"},
            {1, nullptr, "LoadAuthenticationTokenCache"},
            {2, nullptr, "InvalidateAuthenticationTokenCache"},
            {3, nullptr, "IsDeviceAuthenticationTokenCacheAvailable"},
            {10, nullptr, "EnsureEdgeTokenCacheAsync"},
            {11, nullptr, "LoadEdgeTokenCache"},
            {12, nullptr, "InvalidateEdgeTokenCache"},
            {13, nullptr, "IsEdgeTokenCacheAvailable"},
            {20, nullptr, "EnsureApplicationAuthenticationCacheAsync"},
            {21, nullptr, "LoadApplicationAuthenticationTokenCache"},
            {22, nullptr, "LoadApplicationNetworkServiceClientConfigCache"},
            {23, nullptr, "IsApplicationAuthenticationCacheAvailable"},
            {24, nullptr, "InvalidateApplicationAuthenticationCache"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

// 6.0.0+
class IAsyncResult final : public ServiceFramework<IAsyncResult> {
public:
    explicit IAsyncResult(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAsyncResult"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetResult"},
            {1, nullptr, "Cancel"},
            {2, nullptr, "IsAvailable"},
            {3, nullptr, "GetSystemEvent"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void Module::Interface::GetUserCount(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(static_cast<u32>(profile_manager->GetUserCount()));
}

void Module::Interface::GetUserExistence(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(profile_manager->UserExists(user_id));
}

void Module::Interface::ListAllUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListOpenUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetOpenUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetLastOpenedUser(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw<Common::UUID>(profile_manager->GetLastOpenedUser());
}

void Module::Interface::GetProfile(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IProfile>(system, user_id, *profile_manager);
}

void Module::Interface::IsUserRegistrationRequestPermitted(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(profile_manager->CanSystemRegisterUser());
}

void Module::Interface::InitializeApplicationInfo(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

void Module::Interface::InitializeApplicationInfoRestricted(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(Partial implementation) called");

    // TODO(ogniK): We require checking if the user actually owns the title and what not. As of
    // currently, we assume the user owns the title. InitializeApplicationInfoBase SHOULD be called
    // first then we do extra checks if the game is a digital copy.

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

Result Module::Interface::InitializeApplicationInfoBase() {
    if (application_info) {
        LOG_ERROR(Service_ACC, "Application already initialized");
        return Account::ResultApplicationInfoAlreadyInitialized;
    }

    // TODO(ogniK): This should be changed to reflect the target process for when we have multiple
    // processes emulated. As we don't actually have pid support we should assume we're just using
    // our own process
    Glue::ApplicationLaunchProperty launch_property{};
    const auto result = system.GetARPManager().GetLaunchProperty(
        &launch_property, system.GetApplicationProcessProgramID());

    if (result != ResultSuccess) {
        LOG_ERROR(Service_ACC, "Failed to get launch property");
        return Account::ResultInvalidApplication;
    }

    switch (launch_property.base_game_storage_id) {
    case FileSys::StorageId::GameCard:
        application_info.application_type = ApplicationType::GameCard;
        break;
    case FileSys::StorageId::Host:
    case FileSys::StorageId::NandUser:
    case FileSys::StorageId::SdCard:
    case FileSys::StorageId::None: // Suyu specific, differs from hardware
        application_info.application_type = ApplicationType::Digital;
        break;
    default:
        LOG_ERROR(Service_ACC, "Invalid game storage ID! storage_id={}",
                  launch_property.base_game_storage_id);
        return Account::ResultInvalidApplication;
    }

    LOG_WARNING(Service_ACC, "ApplicationInfo init required");
    // TODO(ogniK): Actual initialization here

    return ResultSuccess;
}

void Module::Interface::GetBaasAccountManagerForApplication(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IManagerForApplication>(system, profile_manager);
}

void Module::Interface::IsUserAccountSwitchLocked(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    FileSys::NACP nacp;
    const auto res = system.GetAppLoader().ReadControlData(nacp);

    bool is_locked = false;

    if (res != Loader::ResultStatus::Success) {
        const FileSys::PatchManager pm{system.GetApplicationProcessProgramID(),
                                       system.GetFileSystemController(),
                                       system.GetContentProvider()};
        const auto nacp_unique = pm.GetControlMetadata().first;

        if (nacp_unique != nullptr) {
            is_locked = nacp_unique->GetUserAccountSwitchLock();
        } else {
            LOG_ERROR(Service_ACC, "nacp_unique is null!");
        }
    } else {
        is_locked = nacp.GetUserAccountSwitchLock();
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_locked);
}

void Module::Interface::InitializeApplicationInfoV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::BeginUserRegistration(HLERequestContext& ctx) {
    const auto user_id = Common::UUID::MakeRandom();
    profile_manager->CreateNewUser(user_id, "suyu");

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw(user_id);
}

void Module::Interface::CompleteUserRegistration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    profile_manager->WriteUserSaveFile();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetProfileEditor(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_ACC, "called, user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IProfileEditor>(system, user_id, *profile_manager);
}

void Module::Interface::ListQualifiedUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    // All users should be qualified. We don't actually have parental control or anything to do with
    // nintendo online currently. We're just going to assume the user running the game has access to
    // the game regardless of parental control settings.
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListOpenContextStoredUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    ctx.WriteBuffer(profile_manager->GetStoredOpenedUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::StoreSaveDataThumbnailApplication(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}", uuid.RawString());

    // TODO(ogniK): Check if application ID is zero on acc initialize. As we don't have a reliable
    // way of confirming things like the TID, we're going to assume a non zero value for the time
    // being.
    constexpr u64 tid{1};
    StoreSaveDataThumbnail(ctx, uuid, tid);
}

void Module::Interface::GetBaasAccountManagerForSystemService(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IManagerForSystemService>(system, uuid);
}

void Module::Interface::StoreSaveDataThumbnailSystem(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}, tid={:016X}", uuid.RawString(), tid);
    StoreSaveDataThumbnail(ctx, uuid, tid);
}

void Module::Interface::StoreSaveDataThumbnail(HLERequestContext& ctx, const Common::UUID& uuid,
                                               const u64 tid) {
    IPC::ResponseBuilder rb{ctx, 2};

    if (tid == 0) {
        LOG_ERROR(Service_ACC, "TitleID is not valid!");
        rb.Push(Account::ResultInvalidApplication);
        return;
    }

    if (uuid.IsInvalid()) {
        LOG_ERROR(Service_ACC, "User ID is not valid!");
        rb.Push(Account::ResultInvalidUserId);
        return;
    }
    const auto thumbnail_size = ctx.GetReadBufferSize();
    if (thumbnail_size != THUMBNAIL_SIZE) {
        LOG_ERROR(Service_ACC, "Buffer size is empty! size={:X} expecting {:X}", thumbnail_size,
                  THUMBNAIL_SIZE);
        rb.Push(Account::ResultInvalidArrayLength);
        return;
    }

    // TODO(ogniK): Construct save data thumbnail
    rb.Push(ResultSuccess);
}

void Module::Interface::TrySelectUserWithoutInteraction(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    // A u8 is passed into this function which we can safely ignore. It's to determine if we have
    // access to use the network or not by the looks of it
    IPC::ResponseBuilder rb{ctx, 6};
    if (profile_manager->GetUserCount() != 1) {
        rb.Push(ResultSuccess);
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    const auto user_list = profile_manager->GetAllUsers();
    if (std::ranges::all_of(user_list, [](const auto& user) { return user.IsInvalid(); })) {
        rb.Push(ResultUnknown); // TODO(ogniK): Find the correct error code
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    // Select the first user we have
    rb.Push(ResultSuccess);
    rb.PushRaw(profile_manager->GetUser(0)->uuid);
}

Module::Interface::Interface(std::shared_ptr<Module> module_,
                             std::shared_ptr<ProfileManager> profile_manager_,
                             Core::System& system_, const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)}, profile_manager{std::move(
                                                                       profile_manager_)} {}

Module::Interface::~Interface() = default;

void LoopProcess(Core::System& system) {
    auto module = std::make_shared<Module>();
    auto profile_manager = std::make_shared<ProfileManager>();
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("acc:aa",
                                         std::make_shared<ACC_AA>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:su",
                                         std::make_shared<ACC_SU>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:u0",
                                         std::make_shared<ACC_U0>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:u1",
                                         std::make_shared<ACC_U1>(module, profile_manager, system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Account
