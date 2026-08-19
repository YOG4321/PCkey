#include "input_injector.hpp"

#include <algorithm>
#include <chrono>
#include <new>
#include <thread>
#include <utility>

namespace pckey::core {

struct InputInjector::QueueState {
    std::mutex mutex{};
    std::condition_variable condition{};
    std::array<SyntheticKeyEvent, kQueueCapacity> queue{};
    std::size_t head{};
    std::size_t tail{};
    std::size_t size{};
    bool stopping{};
    bool injection_failed{};
    std::uint64_t generation{};
    std::array<SyntheticKeyEvent, kMaximumInputBatch> in_flight{};
    std::size_t in_flight_count{};
    std::uint64_t in_flight_generation{};
    HWND failure_window{};
    UINT failure_message{};
};

InputInjector::~InputInjector() {
    Stop();

    // Stop() deliberately keeps a timed-out worker handle so a later Start()
    // cannot race an old SendInput call. There is no later Start() after
    // destruction, so it is safe to release that bookkeeping here even when
    // the worker itself is still finishing in the background.
    std::scoped_lock lock(mutex_);
    if (!started_ && thread_ != nullptr) {
        CloseHandle(thread_);
        thread_ = nullptr;
        state_.reset();
    }
}

bool InputInjector::Start(
    const HWND failure_window,
    const UINT failure_message) {
    std::scoped_lock lock(mutex_);
    if (started_) {
        return true;
    }

    // A bounded Stop() can leave a worker blocked inside SendInput. Do not
    // start a second worker until the old one has terminated, otherwise stale
    // events from the old generation could interleave with new input.
    ReapFinishedWorkerLocked();
    if (thread_ != nullptr) {
        return false;
    }

    std::shared_ptr<QueueState> state;
    try {
        state = std::make_shared<QueueState>();
    } catch (...) {
        return false;
    }
    auto* context = new (std::nothrow)
        std::shared_ptr<QueueState>(state);
    if (context == nullptr) {
        return false;
    }

    state->failure_window = failure_window;
    state->failure_message = failure_message;

    thread_ = CreateThread(
        nullptr,
        0,
        &InputInjector::WorkerEntry,
        context,
        0,
        nullptr);
    if (thread_ == nullptr) {
        delete context;
        return false;
    }

    state_ = std::move(state);
    started_ = true;
    return true;
}

bool InputInjector::Enqueue(
    const SyntheticKeyEvent* events,
    const std::size_t count) {
    if (events == nullptr || count == 0) {
        return true;
    }
    if (count > kQueueCapacity) {
        return false;
    }

    bool synchronous_fallback = false;
    std::shared_ptr<QueueState> state;
    {
        std::scoped_lock lock(mutex_);
        if (!started_) {
            // A stopped worker still owns state_ while Stop() is waiting.
            // Do not race it with a synchronous injection; callers can retry
            // once the lifecycle transition has completed.
            synchronous_fallback = state_ == nullptr;
            if (!synchronous_fallback) {
                return false;
            }
        } else {
            state = state_;
            if (state == nullptr) {
                return false;
            }

            std::scoped_lock state_lock(state->mutex);
            if (state->stopping ||
                state->injection_failed ||
                state->size > kQueueCapacity - count) {
                return false;
            }

            const auto first_segment =
                std::min(count, kQueueCapacity - state->tail);
            std::copy(
                events,
                events + first_segment,
                state->queue.begin() + state->tail);
            std::copy(
                events + first_segment,
                events + count,
                state->queue.begin());
            state->tail =
                (state->tail + count) % kQueueCapacity;
            state->size += count;
        }
    }

    if (synchronous_fallback) {
        return InjectSynchronous(events, count);
    }

    state->condition.notify_one();
    return true;
}

bool InputInjector::Enqueue(
    const std::vector<SyntheticKeyEvent>& events) {
    return Enqueue(events.data(), events.size());
}

bool InputInjector::ReplacePending(
    const SyntheticKeyEvent* events,
    const std::size_t count) {
    if (events == nullptr && count != 0) {
        return false;
    }
    if (count > kQueueCapacity - kMaximumInputBatch) {
        return false;
    }

    std::shared_ptr<QueueState> state;
    bool synchronous_fallback = false;
    {
        std::scoped_lock lock(mutex_);
        if (!started_) {
            if (state_ != nullptr) {
                // Stop() is still waiting for the old worker. Do not race it
                // with a synchronous recovery call.
                return false;
            }
            synchronous_fallback = true;
        } else {
            state = state_;
            if (state == nullptr) {
                return false;
            }

            std::scoped_lock state_lock(state->mutex);
            if (state->stopping) {
                return false;
            }

            // Invalidate a batch currently being retried. The worker checks
            // this generation after every SendInput call and abandons stale
            // input before draining the replacement batch.
            ++state->generation;
            state->head = 0;
            state->tail = 0;
            state->size = 0;
            state->injection_failed = false;
            const auto append = [&state](
                                    const SyntheticKeyEvent& event) {
                if (state->size >= kQueueCapacity) {
                    return;
                }
                state->queue[state->tail] = event;
                state->tail =
                    (state->tail + 1) % kQueueCapacity;
                ++state->size;
            };

            // Releases must precede restoration presses, including releases
            // synthesized from an uncertain in-flight batch below.
            for (std::size_t index = 0; index < count; ++index) {
                if (events[index].transition ==
                    KeyTransition::Release) {
                    append(events[index]);
                }
            }

            // The active SendInput call may have inserted only a prefix of
            // its batch. Queue releases for every stateful event that could
            // therefore still be down. Duplicates are harmless and bounded.
            for (std::size_t index = 0;
                 index < state->in_flight_count &&
                 state->size < kQueueCapacity;
                 ++index) {
                auto release = state->in_flight[index];
                if (release.kind != SyntheticEventKind::Keyboard &&
                    release.kind != SyntheticEventKind::VirtualKey &&
                    release.kind != SyntheticEventKind::MouseButton) {
                    continue;
                }
                release.transition = KeyTransition::Release;
                append(release);
            }
            for (std::size_t index = 0; index < count; ++index) {
                if (events[index].transition !=
                    KeyTransition::Release) {
                    append(events[index]);
                }
            }
        }
    }

    if (synchronous_fallback) {
        return InjectSynchronous(events, count);
    }

    state->condition.notify_all();
    return true;
}

bool InputInjector::ReplacePending(
    const std::vector<SyntheticKeyEvent>& events) {
    return ReplacePending(events.data(), events.size());
}

bool InputInjector::FailurePending() noexcept {
    std::shared_ptr<QueueState> state;
    {
        std::scoped_lock lock(mutex_);
        if (!started_) {
            return false;
        }
        state = state_;
    }
    if (state == nullptr) {
        return false;
    }
    std::scoped_lock state_lock(state->mutex);
    return state->injection_failed;
}

void InputInjector::Stop() noexcept {
    HANDLE thread = nullptr;
    std::shared_ptr<QueueState> state;
    {
        std::scoped_lock lock(mutex_);
        if (!started_) {
            ReapFinishedWorkerLocked();
            return;
        }
        thread = thread_;
        state = state_;
        started_ = false;
    }

    if (state != nullptr) {
        {
            std::scoped_lock state_lock(state->mutex);
            state->stopping = true;
            state->failure_window = nullptr;
            state->failure_message = 0;
        }
        state->condition.notify_all();
    }

    // Bounded drain: the worker finishes everything already queued so release
    // events are injected before the hook is removed. If SendInput is stuck,
    // give up after a short wait. The worker retains the queue state, so it is
    // safe for this object to continue or be destroyed while that call is
    // still blocked.
    if (thread != nullptr) {
        (void)WaitForSingleObject(thread, 2000);
    }

    std::scoped_lock lock(mutex_);
    ReapFinishedWorkerLocked();
}

void InputInjector::ReapFinishedWorkerLocked() noexcept {
    if (thread_ == nullptr) {
        return;
    }

    const auto wait_result =
        WaitForSingleObject(thread_, 0);
    if (wait_result == WAIT_TIMEOUT) {
        return;
    }

    if (wait_result == WAIT_OBJECT_0) {
        CloseHandle(thread_);
    }
    thread_ = nullptr;
    state_.reset();
}

DWORD WINAPI InputInjector::WorkerEntry(LPVOID parameter) {
    auto* context =
        static_cast<std::shared_ptr<QueueState>*>(parameter);
    if (context == nullptr) {
        return 0;
    }

    auto state = std::move(*context);
    delete context;
    WorkerLoop(std::move(state));
    return 0;
}

void InputInjector::WorkerLoop(
    std::shared_ptr<QueueState> state) noexcept {
    if (state == nullptr) {
        return;
    }

    std::array<SyntheticKeyEvent, kMaximumInputBatch> local{};

    for (;;) {
        std::size_t count = 0;
        std::uint64_t batch_generation = 0;
        {
            std::unique_lock lock(state->mutex);
            state->condition.wait(
                lock,
                [&state] {
                    return state->stopping || state->size != 0;
                });

            if (state->size == 0 && state->stopping) {
                return;
            }

            count = std::min(state->size, local.size());
            batch_generation = state->generation;
            for (std::size_t index = 0;
                 index < count;
                 ++index) {
                local[index] =
                    state->queue[
                        (state->head + index) % kQueueCapacity];
            }
            state->head =
                (state->head + count) % kQueueCapacity;
            state->size -= count;
        }

        if (count == 0) {
            continue;
        }

        std::array<INPUT, kMaximumInputBatch> inputs{};
        std::size_t input_count = 0;
        BuildInputs(
            local.data(),
            count,
            inputs.data(),
            input_count);
        if (input_count == 0) {
            continue;
        }

        std::size_t sent_total = 0;
        {
            std::scoped_lock state_lock(state->mutex);
            if (state->generation != batch_generation) {
                continue;
            }
            std::copy(
                local.begin(),
                local.begin() + count,
                state->in_flight.begin());
            state->in_flight_count = count;
            state->in_flight_generation = batch_generation;
        }
        for (;;) {
            {
                std::scoped_lock state_lock(state->mutex);
                if (state->generation != batch_generation) {
                    break;
                }
            }

            const auto sent = SendInput(
                static_cast<UINT>(input_count - sent_total),
                inputs.data() + sent_total,
                sizeof(INPUT));
            if (sent > 0) {
                sent_total += std::min<std::size_t>(
                    static_cast<std::size_t>(sent),
                    input_count - sent_total);
            }
            if (sent_total >= input_count) {
                std::scoped_lock state_lock(state->mutex);
                if (state->generation == batch_generation) {
                    state->injection_failed = false;
                }
                if (state->in_flight_generation ==
                    batch_generation) {
                    state->in_flight_count = 0;
                }
                break;
            }

            HWND failure_window = nullptr;
            UINT failure_message = 0;
            bool notify_failure = false;
            {
                std::scoped_lock state_lock(state->mutex);
                if (state->generation != batch_generation ||
                    state->stopping) {
                    break;
                }
                if (!state->injection_failed) {
                    state->injection_failed = true;
                    failure_window = state->failure_window;
                    failure_message = state->failure_message;
                    notify_failure = true;
                }
            }

            if (notify_failure &&
                failure_window != nullptr &&
                failure_message != 0) {
                (void)PostMessageW(
                    failure_window,
                    failure_message,
                    0,
                    0);
            }

            // SendInput may transiently report a short write. Retry the
            // unsent suffix without allowing a stale batch to survive a
            // ReplacePending recovery request.
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        {
            std::scoped_lock state_lock(state->mutex);
            if (state->in_flight_generation == batch_generation &&
                state->generation != batch_generation) {
                state->in_flight_count = 0;
            }
        }
    }
}

bool InputInjector::InjectSynchronous(
    const SyntheticKeyEvent* events,
    const std::size_t count) noexcept {
    if (events == nullptr || count == 0) {
        return true;
    }

    std::size_t offset = 0;
    while (offset < count) {
        std::array<INPUT, 16> inputs{};
        const auto batch_size =
            std::min(inputs.size(), count - offset);

        std::size_t input_count = 0;
        BuildInputs(
            events + offset,
            batch_size,
            inputs.data(),
            input_count);

        const auto sent = SendInput(
            static_cast<UINT>(input_count),
            inputs.data(),
            sizeof(INPUT));
        if (sent != static_cast<UINT>(input_count)) {
            return false;
        }

        offset += batch_size;
    }

    return true;
}

void InputInjector::BuildInputs(
    const SyntheticKeyEvent* events,
    const std::size_t count,
    INPUT* inputs,
    std::size_t& input_count) noexcept {
    input_count = 0;
    for (std::size_t index = 0;
         index < count;
         ++index) {
        const auto& event = events[index];
        if (input_count >= count) {
            break;
        }
        auto& input = inputs[input_count];

        switch (event.kind) {
        case SyntheticEventKind::Keyboard:
            if (!event.key.IsValid()) {
                continue;
            }
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 0;
            input.ki.wScan = event.key.scan_code;
            input.ki.dwFlags = KEYEVENTF_SCANCODE;
            if (event.key.prefix != KeyPrefix::None) {
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }
            if (event.transition == KeyTransition::Release) {
                input.ki.dwFlags |= KEYEVENTF_KEYUP;
            }
            input.ki.dwExtraInfo = kInjectionMarker;
            break;

        case SyntheticEventKind::VirtualKey:
            if (event.virtual_key == 0 ||
                event.virtual_key > 0xFF) {
                continue;
            }
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = event.virtual_key;
            input.ki.wScan = 0;
            input.ki.dwFlags =
                event.transition == KeyTransition::Release
                    ? KEYEVENTF_KEYUP
                    : 0;
            input.ki.dwExtraInfo = kInjectionMarker;
            break;

        case SyntheticEventKind::MouseButton: {
            if (event.mouse_button > MouseButton::X2) {
                continue;
            }
            input.type = INPUT_MOUSE;
            const bool release =
                event.transition == KeyTransition::Release;
            switch (event.mouse_button) {
            case MouseButton::Left:
                input.mi.dwFlags =
                    release
                        ? MOUSEEVENTF_LEFTUP
                        : MOUSEEVENTF_LEFTDOWN;
                break;
            case MouseButton::Right:
                input.mi.dwFlags =
                    release
                        ? MOUSEEVENTF_RIGHTUP
                        : MOUSEEVENTF_RIGHTDOWN;
                break;
            case MouseButton::Middle:
                input.mi.dwFlags =
                    release
                        ? MOUSEEVENTF_MIDDLEUP
                        : MOUSEEVENTF_MIDDLEDOWN;
                break;
            case MouseButton::X1:
            case MouseButton::X2:
                input.mi.dwFlags =
                    release
                        ? MOUSEEVENTF_XUP
                        : MOUSEEVENTF_XDOWN;
                input.mi.mouseData =
                    event.mouse_button == MouseButton::X1
                        ? XBUTTON1
                        : XBUTTON2;
                break;
            }
            input.mi.dwExtraInfo = kInjectionMarker;
            break;
        }

        case SyntheticEventKind::MouseMove:
            input.type = INPUT_MOUSE;
            input.mi.dx = event.mouse_x;
            input.mi.dy = event.mouse_y;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            input.mi.dwExtraInfo = kInjectionMarker;
            break;

        case SyntheticEventKind::MouseWheel:
            input.type = INPUT_MOUSE;
            input.mi.mouseData =
                static_cast<DWORD>(event.mouse_amount);
            input.mi.dwFlags =
                event.horizontal
                    ? MOUSEEVENTF_HWHEEL
                    : MOUSEEVENTF_WHEEL;
            input.mi.dwExtraInfo = kInjectionMarker;
            break;

        default:
            continue;
        }

        ++input_count;
    }
}

}  // namespace pckey::core
