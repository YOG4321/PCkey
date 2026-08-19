#pragma once

#include <Windows.h>

#include <array>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "pckey/mapping_engine.hpp"

namespace pckey::core {

// Dedicated input-injection worker.
//
// SendInput is a synchronous call that can block for a long time (or hang)
// when the foreground application's input queue is stalled. If that call ran
// inside the low-level keyboard hook callback or the message-pump thread, the
// whole core would freeze and keyboard input would stall system-wide.
//
// This class moves every SendInput call to its own thread: producers only
// enqueue synthetic events (bounded, non-blocking) and the worker drains the
// queue. A stuck SendInput can then stall only the worker; the hook and the
// message pump keep running.
class InputInjector {
public:
    InputInjector() = default;
    ~InputInjector();

    InputInjector(const InputInjector&) = delete;
    InputInjector& operator=(const InputInjector&) = delete;

    // Starts the worker thread. On initial creation failure, injection falls
    // back to synchronous SendInput. Returns false while a worker from a
    // bounded Stop() is still finishing, so two workers never overlap.
    bool Start(
        HWND failure_window = nullptr,
        UINT failure_message = 0);

    // Non-blocking. Copies the events and returns true. Returns false when
    // the bounded queue cannot hold them; callers should then let the
    // original physical event through instead of dropping input.
    bool Enqueue(
        const SyntheticKeyEvent* events,
        std::size_t count);

    bool Enqueue(
        const std::vector<SyntheticKeyEvent>& events);

    // Drops events that have not reached SendInput and replaces them with a
    // recovery batch.  This is used after a queue/injection failure so stale
    // presses cannot be delivered after the mapping state has been reset.
    bool ReplacePending(
        const SyntheticKeyEvent* events,
        std::size_t count);

    bool ReplacePending(
        const std::vector<SyntheticKeyEvent>& events);

    [[nodiscard]] bool FailurePending() noexcept;

    // Drains queued events and stops the worker. Bounded wait; safe to call
    // multiple times and from any thread. A subsequent Start() is rejected
    // until a worker that exceeded the wait has finished.
    void Stop() noexcept;

    // Fallback used when the worker thread could not be created.
    static bool InjectSynchronous(
        const SyntheticKeyEvent* events,
        std::size_t count) noexcept;

private:
    struct QueueState;

    static DWORD WINAPI WorkerEntry(LPVOID parameter);
    static void WorkerLoop(
        std::shared_ptr<QueueState> state) noexcept;

    // Reaps a worker left behind by a bounded Stop() once its thread has
    // actually finished. Must be called with mutex_ held.
    void ReapFinishedWorkerLocked() noexcept;

    static void BuildInputs(
        const SyntheticKeyEvent* events,
        std::size_t count,
        INPUT* inputs,
        std::size_t& input_count) noexcept;

    static inline constexpr std::size_t kQueueCapacity = 2048;
    static inline constexpr std::size_t kMaximumInputBatch = 64;
    static inline constexpr ULONG_PTR kInjectionMarker =
        static_cast<ULONG_PTR>(0x50434B45594D4152ULL);

    // The worker owns a shared reference to the queue state. This keeps the
    // worker safe even when Stop() has to return before a blocked SendInput
    // call finishes.
    std::shared_ptr<QueueState> state_{};
    HANDLE thread_{};
    std::mutex mutex_{};
    bool started_{};
};

}  // namespace pckey::core
