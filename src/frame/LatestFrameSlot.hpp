#pragma once

#include "frame/Frame.hpp"
#include "util/Clock.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <utility>

namespace camview {

// Single-producer / single-consumer mailbox that only ever holds the newest frame.
//
// Three Frame buffers circulate: the producer's, the consumer's and the one in the slot.
// publish() and take() just swap buffers under a mutex (O(1), no copying, no allocation), so
// after warm-up the pipeline allocates nothing. Publishing over an unread frame replaces it:
// the older frame is dropped rather than queued, because a late frame is worth less than a
// fresh one.
class LatestFrameSlot {
public:
    // Producer side. Swaps `frame` into the slot; `frame` gets back a spare buffer to fill next.
    void publish(Frame& frame) {
        {
            const std::lock_guard lock(mutex_);
            if (closed_) {
                return;
            }
            std::swap(slot_, frame);
            if (hasNewFrame_) {
                ++dropped_;
            }
            hasNewFrame_ = true;
        }
        newFrame_.notify_one(); // Outside the lock: the woken thread can take it immediately.
    }

    // Consumer side. Waits up to `timeout` for a frame newer than the last one taken and swaps
    // it into `frame`; the consumer's previous buffer becomes the spare. Returns false on
    // timeout, or when the slot is closed and holds nothing new.
    [[nodiscard]] bool take(Frame& frame, Duration timeout) {
        std::unique_lock lock(mutex_);
        newFrame_.wait_for(lock, timeout, [this] { return hasNewFrame_ || closed_; });
        if (!hasNewFrame_) {
            return false;
        }
        std::swap(slot_, frame);
        hasNewFrame_ = false;
        return true;
    }

    // Wakes the consumer and turns further publishes into no-ops (shutdown or camera loss).
    void close() {
        {
            const std::lock_guard lock(mutex_);
            closed_ = true;
        }
        newFrame_.notify_all();
    }

    // Frames replaced by a newer one before the consumer took them.
    [[nodiscard]] std::uint64_t dropped() const {
        const std::lock_guard lock(mutex_);
        return dropped_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable newFrame_;
    Frame slot_;
    bool hasNewFrame_{false};
    bool closed_{false};
    std::uint64_t dropped_{0};
};

} // namespace camview