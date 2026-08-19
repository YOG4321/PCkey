#pragma once

#include <Windows.h>

#include <cwchar>

namespace pckey::editor {

inline void StartupTrace(const wchar_t* stage) noexcept {
    if (stage == nullptr) {
        return;
    }

    wchar_t directory[32768]{};
    const auto length = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        directory,
        static_cast<DWORD>(std::size(directory)));
    if (length == 0 ||
        length + 7 >= std::size(directory)) {
        return;
    }

    wcscat_s(directory, L"\\PCkey");
    CreateDirectoryW(directory, nullptr);

    wchar_t path[32768]{};
    std::swprintf(
        path,
        std::size(path),
        L"%ls\\editor-startup-%lu.log",
        directory,
        GetCurrentProcessId());

    const auto file = CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t wide_line[512]{};
    const auto wide_length = std::swprintf(
        wide_line,
        std::size(wide_line),
        L"%02u:%02u:%02u.%03u pid=%lu tid=%lu %ls\r\n",
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds,
        GetCurrentProcessId(),
        GetCurrentThreadId(),
        stage);
    if (wide_length > 0) {
        char utf8_line[2048]{};
        const auto utf8_length = WideCharToMultiByte(
            CP_UTF8,
            0,
            wide_line,
            wide_length,
            utf8_line,
            static_cast<int>(std::size(utf8_line)),
            nullptr,
            nullptr);
        if (utf8_length > 0) {
            DWORD written = 0;
            WriteFile(
                file,
                utf8_line,
                static_cast<DWORD>(utf8_length),
                &written,
                nullptr);
        }
    }

    CloseHandle(file);
}

}  // namespace pckey::editor
