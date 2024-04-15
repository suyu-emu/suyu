// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/vulkan_common/vulkan_wrapper.h"

#ifdef _WIN32
#include <cstring>
#include <QByteArray>
#include <processthreadsapi.h>
#include <windows.h>
#elif defined(SUYU_UNIX)
#include <cstring>
#include <errno.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <fmt/core.h>
#include "suyu/startup_checks.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"

void CheckVulkan() {
    // Just start the Vulkan loader, this will crash if something is wrong
    try {
        Vulkan::vk::InstanceDispatch dld;
        const auto library = Vulkan::OpenLibrary();
        const Vulkan::vk::Instance instance =
            Vulkan::CreateInstance(*library, dld, VK_API_VERSION_1_1);

    } catch (const Vulkan::vk::Exception& exception) {
        fmt::print(stderr, "Failed to initialize Vulkan: {}\n", exception.what());
    }
}

bool CheckEnvVars(bool* is_child) {
#ifdef _WIN32
    // Check environment variable to see if we are the child
    char variable_contents[8];
    const DWORD startup_check_var =
        GetEnvironmentVariableA(STARTUP_CHECK_ENV_VAR, variable_contents, 8);
    if (startup_check_var > 0 && std::strncmp(variable_contents, ENV_VAR_ENABLED_TEXT, 8) == 0) {
        CheckVulkan();
        return true;
    }

    // Don't perform startup checks if we are a child process
    char is_child_s[8];
    const DWORD is_child_len = GetEnvironmentVariableA(IS_CHILD_ENV_VAR, is_child_s, 8);
    if (is_child_len > 0 && std::strncmp(is_child_s, ENV_VAR_ENABLED_TEXT, 8) == 0) {
        *is_child = true;
        return false;
    } else if (!SetEnvironmentVariableA(IS_CHILD_ENV_VAR, ENV_VAR_ENABLED_TEXT)) {
        fmt::print(stderr, "SetEnvironmentVariableA failed to set {} with error {}\n",
                   IS_CHILD_ENV_VAR, GetLastError());
        return true;
    }
#elif defined(SUYU_UNIX)
    const char* startup_check_var = getenv(STARTUP_CHECK_ENV_VAR);
    if (startup_check_var != nullptr &&
        std::strncmp(startup_check_var, ENV_VAR_ENABLED_TEXT, 8) == 0) {
        CheckVulkan();
        return true;
    }
#endif
    return false;
}

bool StartupChecks(const char* arg0, bool* has_broken_vulkan, bool perform_vulkan_check) {
#ifdef _WIN32
    // Set the startup variable for child processes
    const bool env_var_set = SetEnvironmentVariableA(STARTUP_CHECK_ENV_VAR, ENV_VAR_ENABLED_TEXT);
    if (!env_var_set) {
        fmt::print(stderr, "SetEnvironmentVariableA failed to set {} with error {}\n",
                   STARTUP_CHECK_ENV_VAR, GetLastError());
        return false;
    }

    if (perform_vulkan_check) {
        // Spawn child process that performs Vulkan check
        PROCESS_INFORMATION process_info;
        std::memset(&process_info, '\0', sizeof(process_info));

        if (!SpawnChild(arg0, &process_info, 0)) {
            return false;
        }

        // Wait until the process exits and get exit code from it
        WaitForSingleObject(process_info.hProcess, INFINITE);
        DWORD exit_code = STILL_ACTIVE;
        const int err = GetExitCodeProcess(process_info.hProcess, &exit_code);
        if (err == 0) {
            fmt::print(stderr, "GetExitCodeProcess failed with error {}\n", GetLastError());
        }

        // Vulkan is broken if the child crashed (return value is not zero)
        *has_broken_vulkan = (exit_code != 0);

        if (CloseHandle(process_info.hProcess) == 0) {
            fmt::print(stderr, "CloseHandle failed with error {}\n", GetLastError());
        }
        if (CloseHandle(process_info.hThread) == 0) {
            fmt::print(stderr, "CloseHandle failed with error {}\n", GetLastError());
        }
    }

    if (!SetEnvironmentVariableA(STARTUP_CHECK_ENV_VAR, nullptr)) {
        fmt::print(stderr, "SetEnvironmentVariableA failed to clear {} with error {}\n",
                   STARTUP_CHECK_ENV_VAR, GetLastError());
    }

#elif defined(SUYU_UNIX)
    const int env_var_set = setenv(STARTUP_CHECK_ENV_VAR, ENV_VAR_ENABLED_TEXT, 1);
    if (env_var_set == -1) {
        const int err = errno;
        fmt::print(stderr, "setenv failed to set {} with error {}\n", STARTUP_CHECK_ENV_VAR, err);
        return false;
    }

    if (perform_vulkan_check) {
        const pid_t pid = SpawnChild(arg0);
        if (pid == -1) {
            return false;
        }

        // Get exit code from child process
        int status;
        const int r_val = waitpid(pid, &status, 0);
        if (r_val == -1) {
            const int err = errno;
            fmt::print(stderr, "wait failed with error {}\n", err);
            return false;
        }
        // Vulkan is broken if the child crashed (return value is not zero)
        *has_broken_vulkan = (status != 0);
    }

    const int env_var_cleared = unsetenv(STARTUP_CHECK_ENV_VAR);
    if (env_var_cleared == -1) {
        const int err = errno;
        fmt::print(stderr, "unsetenv failed to clear {} with error {}\n", STARTUP_CHECK_ENV_VAR,
                   err);
    }
#endif
    return false;
}

#ifdef _WIN32
bool SpawnChild(const char* arg0, PROCESS_INFORMATION* pi, int flags) {
    STARTUPINFOA startup_info;

    std::memset(&startup_info, '\0', sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    char p_name[255];
    std::strncpy(p_name, arg0, 254);
    p_name[254] = '\0';

    const bool process_created = CreateProcessA(nullptr,       // lpApplicationName
                                                p_name,        // lpCommandLine
                                                nullptr,       // lpProcessAttributes
                                                nullptr,       // lpThreadAttributes
                                                false,         // bInheritHandles
                                                flags,         // dwCreationFlags
                                                nullptr,       // lpEnvironment
                                                nullptr,       // lpCurrentDirectory
                                                &startup_info, // lpStartupInfo
                                                pi             // lpProcessInformation
    );
    if (!process_created) {
        fmt::print(stderr, "CreateProcessA failed with error {}\n", GetLastError());
        return false;
    }

    return true;
}
#elif defined(SUYU_UNIX)
pid_t SpawnChild(const char* arg0) {
    const pid_t pid = fork();

    if (pid == -1) {
        // error
        const int err = errno;
        fmt::print(stderr, "fork failed with error {}\n", err);
        return pid;
    } else if (pid == 0) {
        // child
        execlp(arg0, arg0, nullptr);
        const int err = errno;
        fmt::print(stderr, "execl failed with error {}\n", err);
        _exit(0);
    }

    return pid;
}
#endif
