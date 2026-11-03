#include "frame/LatestFrameSlot.hpp"

#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>

namespace camview {
namespace {

using namespace std::chrono_literals;

Frame frameWithSequence(std::uint32_t sequence) {
    Frame frame;
    frame.sequence = sequence;
    frame.data.resize(16);
    return frame;
}

TEST(LatestFrameSlot, TakeTimesOutWhenNothingWasPublished) {
    LatestFrameSlot slot;
    Frame frame;
    EXPECT_FALSE(slot.take(frame, 10ms));
}

TEST(LatestFrameSlot, DeliversThePublishedFrame) {
    LatestFrameSlot slot;
    Frame produced = frameWithSequence(7);
    slot.publish(produced);

    Frame consumed;
    ASSERT_TRUE(slot.take(consumed, 10ms));
    EXPECT_EQ(consumed.sequence, 7U);
}

TEST(LatestFrameSlot, KeepsOnlyTheNewestFrame) {
    LatestFrameSlot slot;
    for (std::uint32_t sequence = 1; sequence <= 3; ++sequence) {
        Frame produced = frameWithSequence(sequence);
        slot.publish(produced);
    }

    Frame consumed;
    ASSERT_TRUE(slot.take(consumed, 10ms));
    EXPECT_EQ(consumed.sequence, 3U);
    EXPECT_EQ(slot.dropped(), 2U);
    EXPECT_FALSE(slot.take(consumed, 10ms)); // Nothing newer since the last take.
}

TEST(LatestFrameSlot, HandsOverTheBufferWithoutCopyingIt) {
    LatestFrameSlot slot;
    Frame produced = frameWithSequence(1);
    const std::uint8_t* buffer = produced.data.data();
    slot.publish(produced);

    Frame consumed;
    ASSERT_TRUE(slot.take(consumed, 10ms));
    EXPECT_EQ(consumed.data.data(), buffer); // Same memory: swapped, not copied.
}

TEST(LatestFrameSlot, CloseWakesAWaitingConsumerImmediately) {
    LatestFrameSlot slot;
    std::thread closer([&slot] {
        std::this_thread::sleep_for(50ms);
        slot.close();
    });

    Frame frame;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(slot.take(frame, 5s));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
    closer.join();
}

TEST(LatestFrameSlot, IgnoresPublishesAfterClose) {
    LatestFrameSlot slot;
    slot.close();
    Frame produced = frameWithSequence(1);
    slot.publish(produced);

    Frame consumed;
    EXPECT_FALSE(slot.take(consumed, 10ms));
}

// Run under ThreadSanitizer this also checks the hand-over for data races.
TEST(LatestFrameSlot, ConsumerNeverSeesSequenceNumbersGoBackwards) {
    constexpr std::uint32_t kFrames = 10000;
    LatestFrameSlot slot;

    std::thread producer([&slot] {
        Frame frame;
        frame.data.resize(64);
        for (std::uint32_t sequence = 1; sequence <= kFrames; ++sequence) {
            frame.sequence = sequence;
            slot.publish(frame);
        }
        slot.close();
    });

    Frame consumed;
    std::uint32_t lastSequence = 0;
    std::uint32_t taken = 0;
    while (slot.take(consumed, 1s)) {
        EXPECT_GT(consumed.sequence, lastSequence);
        lastSequence = consumed.sequence;
        ++taken;
    }
    producer.join();

    EXPECT_GT(taken, 0U);
    EXPECT_EQ(taken + slot.dropped(), kFrames);
}

} // namespace
} // namespace camview