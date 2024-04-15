// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include "common/fs/fs.h"
#ifdef ANDROID
#include "common/fs/fs_android.h"
#endif
#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"

#ifdef _WIN32
#include <shlobj.h> // Used in GetExeDirectory()
#else
#include <cstdlib>     // Used in Get(Home/Data)Directory()
#include <pwd.h>       // Used in GetHomeDirectory()
#include <sys/types.h> // Used in GetHomeDirectory()
#include <unistd.h>    // Used in GetDataDirectory()
#endif

#ifdef __APPLE__
#include <sys/param.h> // Used in GetBundleDirectory()

// CFURL contains __attribute__ directives that gcc does not know how to parse, so we need to just
// ignore them if we're not using clang. The macro is only used to prevent linking against
// functions that don't exist on older versions of macOS, and the worst case scenario is a linker
// error, so this is perfectly safe, just inconvenient.
#ifndef __clang__
#define availability(...)
#endif
#include <CoreFoundation/CFBundle.h> // Used in GetBundleDirectory()
#include <CoreFoundation/CFString.h> // Used in GetBundleDirectory()
#include <CoreFoundation/CFURL.h>    // Used in GetBundleDirectory()
#ifdef availability
#undef availability
#endif
#endif

#ifndef MAX_PATH
#ifdef _WIN32
// This is the maximum number of UTF-16 code units permissible in Windows file paths
#define MAX_PATH 260
#else
// This is the maximum number of UTF-8 code units permissible in all other OSes' file paths
#define MAX_PATH 1024
#endif
#endif

namespace Common::FS {

namespace fs = std::filesystem;

/**
 * The PathManagerImpl is a singleton allowing to manage the mapping of
 * SuyuPath enums to real filesystem paths.
 * This class provides 2 functions: GetSuyuPathImpl and SetSuyuPathImpl.
 * These are used by GetSuyuPath and SetSuyuPath respectively to get or modify
 * the path mapped by the SuyuPath enum.
 */
class PathManagerImpl {
public:
    static PathManagerImpl& GetInstance() {
        static PathManagerImpl path_manager_impl;

        return path_manager_impl;
    }

    PathManagerImpl(const PathManagerImpl&) = delete;
    PathManagerImpl& operator=(const PathManagerImpl&) = delete;

    PathManagerImpl(PathManagerImpl&&) = delete;
    PathManagerImpl& operator=(PathManagerImpl&&) = delete;

    [[nodiscard]] const fs::path& GetSuyuPathImpl(SuyuPath suyu_path) {
        return suyu_paths.at(suyu_path);
    }

    void SetSuyuPathImpl(SuyuPath suyu_path, const fs::path& new_path) {
        suyu_paths.insert_or_assign(suyu_path, new_path);
    }

    void Reinitialize(fs::path suyu_path = {}) {
        fs::path suyu_path_cache;
        fs::path suyu_path_config;

#ifdef _WIN32
#ifdef SUYU_ENABLE_PORTABLE
        suyu_path = GetExeDirectory() / PORTABLE_DIR;
#endif
        if (!IsDir(suyu_path)) {
            suyu_path = GetAppDataRoamingDirectory() / SUYU_DIR;
        }

        suyu_path_cache = suyu_path / CACHE_DIR;
        suyu_path_config = suyu_path / CONFIG_DIR;
#elif ANDROID
        ASSERT(!suyu_path.empty());
        suyu_path_cache = suyu_path / CACHE_DIR;
        suyu_path_config = suyu_path / CONFIG_DIR;
#else
#ifdef SUYU_ENABLE_PORTABLE
        suyu_path = GetCurrentDir() / PORTABLE_DIR;
#endif
        if (Exists(suyu_path) && IsDir(suyu_path)) {
            suyu_path_cache = suyu_path / CACHE_DIR;
            suyu_path_config = suyu_path / CONFIG_DIR;
        } else {
            suyu_path = GetDataDirectory("XDG_DATA_HOME") / SUYU_DIR;
            suyu_path_cache = GetDataDirectory("XDG_CACHE_HOME") / SUYU_DIR;
            suyu_path_config = GetDataDirectory("XDG_CONFIG_HOME") / SUYU_DIR;
        }
#endif

        GenerateSuyuPath(SuyuPath::SuyuDir, suyu_path);
        GenerateSuyuPath(SuyuPath::AmiiboDir, suyu_path / AMIIBO_DIR);
        GenerateSuyuPath(SuyuPath::CacheDir, suyu_path_cache);
        GenerateSuyuPath(SuyuPath::ConfigDir, suyu_path_config);
        GenerateSuyuPath(SuyuPath::CrashDumpsDir, suyu_path / CRASH_DUMPS_DIR);
        GenerateSuyuPath(SuyuPath::DumpDir, suyu_path / DUMP_DIR);
        GenerateSuyuPath(SuyuPath::IconsDir, suyu_path / ICONS_DIR);
        GenerateSuyuPath(SuyuPath::KeysDir, suyu_path / KEYS_DIR);
        GenerateSuyuPath(SuyuPath::LoadDir, suyu_path / LOAD_DIR);
        GenerateSuyuPath(SuyuPath::LogDir, suyu_path / LOG_DIR);
        GenerateSuyuPath(SuyuPath::NANDDir, suyu_path / NAND_DIR);
        GenerateSuyuPath(SuyuPath::PlayTimeDir, suyu_path / PLAY_TIME_DIR);
        GenerateSuyuPath(SuyuPath::ScreenshotsDir, suyu_path / SCREENSHOTS_DIR);
        GenerateSuyuPath(SuyuPath::SDMCDir, suyu_path / SDMC_DIR);
        GenerateSuyuPath(SuyuPath::ShaderDir, suyu_path / SHADER_DIR);
        GenerateSuyuPath(SuyuPath::TASDir, suyu_path / TAS_DIR);
        GenerateSuyuPath(SuyuPath::ThemesDir, suyu_path / THEMES_DIR);
    }

private:
    PathManagerImpl() {
        Reinitialize();
    }

    ~PathManagerImpl() = default;

    void GenerateSuyuPath(SuyuPath suyu_path, const fs::path& new_path) {
        void(FS::CreateDir(new_path));

        SetSuyuPathImpl(suyu_path, new_path);
    }

    std::unordered_map<SuyuPath, fs::path> suyu_paths;
};

bool ValidatePath(const fs::path& path) {
    if (path.empty()) {
        LOG_ERROR(Common_Filesystem, "Input path is empty, path={}", PathToUTF8String(path));
        return false;
    }

#ifdef _WIN32
    if (path.u16string().size() >= MAX_PATH) {
        LOG_ERROR(Common_Filesystem, "Input path is too long, path={}", PathToUTF8String(path));
        return false;
    }
#else
    if (path.u8string().size() >= MAX_PATH) {
        LOG_ERROR(Common_Filesystem, "Input path is too long, path={}", PathToUTF8String(path));
        return false;
    }
#endif

    return true;
}

fs::path ConcatPath(const fs::path& first, const fs::path& second) {
    const bool second_has_dir_sep = IsDirSeparator(second.u8string().front());

    if (!second_has_dir_sep) {
        return (first / second).lexically_normal();
    }

    fs::path concat_path = first;
    concat_path += second;

    return concat_path.lexically_normal();
}

fs::path ConcatPathSafe(const fs::path& base, const fs::path& offset) {
    const auto concatenated_path = ConcatPath(base, offset);

    if (!IsPathSandboxed(base, concatenated_path)) {
        return base;
    }

    return concatenated_path;
}

bool IsPathSandboxed(const fs::path& base, const fs::path& path) {
    const auto base_string = RemoveTrailingSeparators(base.lexically_normal()).u8string();
    const auto path_string = RemoveTrailingSeparators(path.lexically_normal()).u8string();

    if (path_string.size() < base_string.size()) {
        return false;
    }

    return base_string.compare(0, base_string.size(), path_string, 0, base_string.size()) == 0;
}

bool IsDirSeparator(char character) {
    return character == '/' || character == '\\';
}

bool IsDirSeparator(char8_t character) {
    return character == u8'/' || character == u8'\\';
}

fs::path RemoveTrailingSeparators(const fs::path& path) {
    if (path.empty()) {
        return path;
    }

    auto string_path = path.u8string();

    while (IsDirSeparator(string_path.back())) {
        string_path.pop_back();
    }

    return fs::path{string_path};
}

void SetAppDirectory(const std::string& app_directory) {
    PathManagerImpl::GetInstance().Reinitialize(app_directory);
}

const fs::path& GetSuyuPath(SuyuPath suyu_path) {
    return PathManagerImpl::GetInstance().GetSuyuPathImpl(suyu_path);
}

std::string GetSuyuPathString(SuyuPath suyu_path) {
    return PathToUTF8String(GetSuyuPath(suyu_path));
}

void SetSuyuPath(SuyuPath suyu_path, const fs::path& new_path) {
    if (!FS::IsDir(new_path)) {
        LOG_ERROR(Common_Filesystem, "Filesystem object at new_path={} is not a directory",
                  PathToUTF8String(new_path));
        return;
    }

    PathManagerImpl::GetInstance().SetSuyuPathImpl(suyu_path, new_path);
}

#ifdef _WIN32

fs::path GetExeDirectory() {
    wchar_t exe_path[MAX_PATH];

    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) {
        LOG_ERROR(Common_Filesystem,
                  "Failed to get the path to the executable of the current process");
    }

    return fs::path{exe_path}.parent_path();
}

fs::path GetAppDataRoamingDirectory() {
    PWSTR appdata_roaming_path = nullptr;

    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata_roaming_path);

    auto fs_appdata_roaming_path = fs::path{appdata_roaming_path};

    CoTaskMemFree(appdata_roaming_path);

    if (fs_appdata_roaming_path.empty()) {
        LOG_ERROR(Common_Filesystem, "Failed to get the path to the %APPDATA% directory");
    }

    return fs_appdata_roaming_path;
}

#else

fs::path GetHomeDirectory() {
    const char* home_env_var = getenv("HOME");

    if (home_env_var) {
        return fs::path{home_env_var};
    }

    LOG_INFO(Common_Filesystem,
             "$HOME is not defined in the environment variables, "
             "attempting to query passwd to get the home path of the current user");

    const auto* pw = getpwuid(getuid());

    if (!pw) {
        LOG_ERROR(Common_Filesystem, "Failed to get the home path of the current user");
        return {};
    }

    return fs::path{pw->pw_dir};
}

fs::path GetDataDirectory(const std::string& env_name) {
    const char* data_env_var = getenv(env_name.c_str());

    if (data_env_var) {
        return fs::path{data_env_var};
    }

    if (env_name == "XDG_DATA_HOME") {
        return GetHomeDirectory() / ".local/share";
    } else if (env_name == "XDG_CACHE_HOME") {
        return GetHomeDirectory() / ".cache";
    } else if (env_name == "XDG_CONFIG_HOME") {
        return GetHomeDirectory() / ".config";
    }

    return {};
}

#endif

#ifdef __APPLE__

fs::path GetBundleDirectory() {
    char app_bundle_path[MAXPATHLEN];

    // Get the main bundle for the app
    CFURLRef bundle_ref = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    CFStringRef bundle_path = CFURLCopyFileSystemPath(bundle_ref, kCFURLPOSIXPathStyle);

    CFStringGetFileSystemRepresentation(bundle_path, app_bundle_path, sizeof(app_bundle_path));

    CFRelease(bundle_ref);
    CFRelease(bundle_path);

    return fs::path{app_bundle_path};
}

#endif

// vvvvvvvvvv Deprecated vvvvvvvvvv //

std::string_view RemoveTrailingSlash(std::string_view path) {
    if (path.empty()) {
        return path;
    }

    if (path.back() == '\\' || path.back() == '/') {
        path.remove_suffix(1);
        return path;
    }

    return path;
}

template <typename F>
static void ForEachPathComponent(std::string_view filename, F&& cb) {
    const char* component_begin = filename.data();
    const char* const end = component_begin + filename.size();
    for (const char* it = component_begin; it != end; ++it) {
        const char c = *it;
        if (c == '\\' || c == '/') {
            if (component_begin != it) {
                cb(std::string_view{component_begin, it});
            }
            component_begin = it + 1;
        }
    }
    if (component_begin != end) {
        cb(std::string_view{component_begin, end});
    }
}

std::vector<std::string_view> SplitPathComponents(std::string_view filename) {
    std::vector<std::string_view> components;
    ForEachPathComponent(filename, [&](auto component) { components.emplace_back(component); });

    return components;
}

std::vector<std::string> SplitPathComponentsCopy(std::string_view filename) {
    std::vector<std::string> components;
    ForEachPathComponent(filename, [&](auto component) { components.emplace_back(component); });

    return components;
}

std::string SanitizePath(std::string_view path_, DirectorySeparator directory_separator) {
    std::string path(path_);
#ifdef ANDROID
    if (Android::IsContentUri(path)) {
        return path;
    }
#endif // ANDROID

    char type1 = directory_separator == DirectorySeparator::BackwardSlash ? '/' : '\\';
    char type2 = directory_separator == DirectorySeparator::BackwardSlash ? '\\' : '/';

    if (directory_separator == DirectorySeparator::PlatformDefault) {
#ifdef _WIN32
        type1 = '/';
        type2 = '\\';
#endif
    }

    std::replace(path.begin(), path.end(), type1, type2);

    auto start = path.begin();
#ifdef _WIN32
    // allow network paths which start with a double backslash (e.g. \\server\share)
    if (start != path.end())
        ++start;
#endif
    path.erase(std::unique(start, path.end(),
                           [type2](char c1, char c2) { return c1 == type2 && c2 == type2; }),
               path.end());
    return std::string(RemoveTrailingSlash(path));
}

std::string GetParentPath(std::string_view path) {
    if (path.empty()) {
        return std::string(path);
    }

#ifdef ANDROID
    if (path[0] != '/') {
        std::string path_string{path};
        return FS::Android::GetParentDirectory(path_string);
    }
#endif
    const auto name_bck_index = path.rfind('\\');
    const auto name_fwd_index = path.rfind('/');
    std::size_t name_index;

    if (name_bck_index == std::string_view::npos || name_fwd_index == std::string_view::npos) {
        name_index = std::min(name_bck_index, name_fwd_index);
    } else {
        name_index = std::max(name_bck_index, name_fwd_index);
    }

    return std::string(path.substr(0, name_index));
}

std::string_view GetPathWithoutTop(std::string_view path) {
    if (path.empty()) {
        return path;
    }

    while (path[0] == '\\' || path[0] == '/') {
        path.remove_prefix(1);
        if (path.empty()) {
            return path;
        }
    }

    const auto name_bck_index = path.find('\\');
    const auto name_fwd_index = path.find('/');
    return path.substr(std::min(name_bck_index, name_fwd_index) + 1);
}

std::string_view GetFilename(std::string_view path) {
    const auto name_index = path.find_last_of("\\/");

    if (name_index == std::string_view::npos) {
        return {};
    }

    return path.substr(name_index + 1);
}

std::string_view GetExtensionFromFilename(std::string_view name) {
    const std::size_t index = name.rfind('.');

    if (index == std::string_view::npos) {
        return {};
    }

    return name.substr(index + 1);
}

} // namespace Common::FS
