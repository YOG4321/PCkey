#include "ipc_server.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "pckey/ipc_protocol.hpp"

namespace pckey::core {

IpcServer::~IpcServer() {
    Stop();
}

bool IpcServer::Start(
    const HWND target_window,
    const UINT reload_message) {
    if (thread_.joinable() ||
        target_window == nullptr ||
        reload_message == 0) {
        return false;
    }

    target_window_ = target_window;
    reload_message_ = reload_message;
    stopping_.store(false, std::memory_order_release);
    startup_error_.store(
        ERROR_SUCCESS,
        std::memory_order_release);
    ready_event_ = CreateEventW(
        nullptr,
        TRUE,
        FALSE,
        nullptr);
    if (ready_event_ == nullptr) {
        return false;
    }

    try {
        thread_ = std::thread([this] {
            ServerLoop();
        });
    } catch (...) {
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
        target_window_ = nullptr;
        reload_message_ = 0;
        return false;
    }

    const auto wait_result =
        WaitForSingleObject(ready_event_, 3000);
    const bool started =
        wait_result == WAIT_OBJECT_0 &&
        startup_error_.load(std::memory_order_acquire) ==
            ERROR_SUCCESS;

    if (!started) {
        Stop();
    }

    return started;
}

void IpcServer::Stop() noexcept {
    if (!thread_.joinable()) {
        return;
    }

    stopping_.store(true, std::memory_order_release);

    const auto wake_pipe = CreateFileW(
        ipc::kPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (wake_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(wake_pipe);
    }

    thread_.join();

    if (ready_event_ != nullptr) {
        CloseHandle(ready_event_);
        ready_event_ = nullptr;
    }

    target_window_ = nullptr;
    reload_message_ = 0;
}

void IpcServer::ServerLoop() {
    bool startup_reported = false;

    while (!stopping_.load(std::memory_order_acquire)) {
        const auto pipe = CreateNamedPipeW(
            ipc::kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE |
                PIPE_READMODE_MESSAGE |
                PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1,
            ipc::kMaximumPayloadSize,
            ipc::kMaximumPayloadSize,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            startup_error_.store(
                GetLastError(),
                std::memory_order_release);
            if (!startup_reported &&
                ready_event_ != nullptr) {
                SetEvent(ready_event_);
            }
            return;
        }

        if (!startup_reported && ready_event_ != nullptr) {
            startup_reported = true;
            SetEvent(ready_event_);
        }

        const bool connected =
            ConnectNamedPipe(pipe, nullptr) != FALSE ||
            GetLastError() == ERROR_PIPE_CONNECTED;

        if (connected &&
            !stopping_.load(std::memory_order_acquire)) {
            HandleClient(pipe);
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void IpcServer::HandleClient(const HANDLE pipe) {
    ipc::Header header{};
    ipc::Response response{};

    if (!ReadExact(
            pipe,
            &header,
            static_cast<DWORD>(sizeof(header))) ||
        header.magic != ipc::kMagic ||
        header.version != ipc::kProtocolVersion ||
        header.payload_size > ipc::kMaximumPayloadSize) {
        response.status = ipc::Status::InvalidRequest;
        (void)WriteExact(
            pipe,
            &response,
            static_cast<DWORD>(sizeof(response)));
        return;
    }

    std::vector<std::byte> payload(header.payload_size);
    if (!payload.empty() &&
        !ReadExact(
            pipe,
            payload.data(),
            header.payload_size)) {
        response.status = ipc::Status::InvalidRequest;
        (void)WriteExact(
            pipe,
            &response,
            static_cast<DWORD>(sizeof(response)));
        return;
    }

    switch (header.command) {
    case ipc::Command::Ping:
        response.status = ipc::Status::Ok;
        break;

    case ipc::Command::ReloadConfiguration: {
        if (payload.size() % sizeof(wchar_t) != 0) {
            response.status = ipc::Status::InvalidRequest;
            break;
        }

        const auto character_count =
            payload.size() / sizeof(wchar_t);
        ReloadConfigurationRequest request{
            std::wstring(character_count, L'\0'),
            false};
        if (!payload.empty()) {
            std::memcpy(
                request.profile_name.data(),
                payload.data(),
                payload.size());
        }

        DWORD_PTR message_result = 0;
        const auto delivered = SendMessageTimeoutW(
            target_window_,
            reload_message_,
            0,
            reinterpret_cast<LPARAM>(&request),
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            3000,
            &message_result);

        response.status =
            delivered != 0 && request.succeeded
                ? ipc::Status::Ok
                : ipc::Status::InvalidConfiguration;
        break;
    }

    default:
        response.status = ipc::Status::InvalidRequest;
        break;
    }

    (void)WriteExact(
        pipe,
        &response,
        static_cast<DWORD>(sizeof(response)));
}

bool IpcServer::ReadExact(
    const HANDLE pipe,
    void* destination,
    const DWORD byte_count) const noexcept {
    auto* output =
        static_cast<std::byte*>(destination);
    DWORD total = 0;

    while (total < byte_count) {
        DWORD received = 0;
        if (ReadFile(
                pipe,
                output + total,
                byte_count - total,
                &received,
                nullptr) == FALSE ||
            received == 0) {
            return false;
        }
        total += received;
    }

    return true;
}

bool IpcServer::WriteExact(
    const HANDLE pipe,
    const void* source,
    const DWORD byte_count) const noexcept {
    const auto* input =
        static_cast<const std::byte*>(source);
    DWORD total = 0;

    while (total < byte_count) {
        DWORD written = 0;
        if (WriteFile(
                pipe,
                input + total,
                byte_count - total,
                &written,
                nullptr) == FALSE ||
            written == 0) {
            return false;
        }
        total += written;
    }

    return true;
}

}  // namespace pckey::core
