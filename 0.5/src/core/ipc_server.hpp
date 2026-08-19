#pragma once

#include <Windows.h>

#include <atomic>
#include <string>
#include <thread>

namespace pckey::core {

struct ReloadConfigurationRequest {
    std::wstring profile_name;
    bool succeeded{};
};

class IpcServer {
public:
    IpcServer() = default;
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool Start(HWND target_window, UINT reload_message);
    void Stop() noexcept;

    [[nodiscard]] DWORD startup_error() const noexcept {
        return startup_error_.load(std::memory_order_acquire);
    }

private:
    void ServerLoop();
    void HandleClient(HANDLE pipe);

    [[nodiscard]] bool ReadExact(
        HANDLE pipe,
        void* destination,
        DWORD byte_count) const noexcept;

    [[nodiscard]] bool WriteExact(
        HANDLE pipe,
        const void* source,
        DWORD byte_count) const noexcept;

    HWND target_window_{};
    UINT reload_message_{};
    std::atomic_bool stopping_{false};
    std::atomic<DWORD> startup_error_{ERROR_SUCCESS};
    HANDLE ready_event_{};
    std::thread thread_{};
};

}  // namespace pckey::core
