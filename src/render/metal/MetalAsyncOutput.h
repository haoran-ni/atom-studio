#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace atom::render::metal {

/// Number of buffered output texture slots per renderer. Three slots allow
/// one frame in flight, one ready for presentation, and one being presented
/// by the Qt scene graph, without ever blocking the render thread.
inline constexpr int kOutputSlotCount = 3;

enum class OutputSlotState : uint8_t {
    Free = 0,
    InFlight = 1,
    Ready = 2
};

/// Bookkeeping shared between the render thread and Metal
/// command-buffer completion handlers. Held via shared_ptr so a completion
/// handler outliving the renderer stays safe.
struct AsyncFrameState {
    // Serialize compound completion publication with resize/reset. Never held
    // while encoding, allocating textures, or waiting for the GPU.
    std::mutex publicationMutex;
    std::atomic<bool> failed{false};
    std::atomic<uint64_t> generation{1};
    std::atomic<int> inFlightSlot{-1};
    std::atomic<int> latestReadySlot{-1};
    std::atomic<int> latestReadySampleCount{0};
    /// Measured GPU nanoseconds per accumulation sample from the most
    /// recently completed command buffer (0 = no measurement yet).
    std::atomic<uint64_t> gpuNanosPerSample{0};
    std::array<std::atomic<uint8_t>, kOutputSlotCount> slotStates{};

    AsyncFrameState() {
        for (auto& state : slotStates) {
            state.store(static_cast<uint8_t>(OutputSlotState::Free));
        }
    }
};

/// Picks a writable output slot, or -1 while a frame is in flight or no slot
/// is reusable. Never returns the latest ready slot or the slot currently
/// presented by the scene graph.
inline int acquireOutputSlot(const AsyncFrameState& state, int lastPresentedSlot) {
    if (state.inFlightSlot.load(std::memory_order_acquire) >= 0) {
        return -1;
    }

    const int latestReadySlot = state.latestReadySlot.load(std::memory_order_acquire);
    const auto isReusable = [&](int slotIndex) {
        return slotIndex != latestReadySlot && slotIndex != lastPresentedSlot;
    };

    for (int slotIndex = 0; slotIndex < kOutputSlotCount; ++slotIndex) {
        const auto slotState = static_cast<OutputSlotState>(
            state.slotStates[static_cast<size_t>(slotIndex)].load(std::memory_order_acquire));
        if (slotState == OutputSlotState::Free && isReusable(slotIndex)) {
            return slotIndex;
        }
    }

    for (int slotIndex = 0; slotIndex < kOutputSlotCount; ++slotIndex) {
        const auto slotState = static_cast<OutputSlotState>(
            state.slotStates[static_cast<size_t>(slotIndex)].load(std::memory_order_acquire));
        if (slotState == OutputSlotState::Ready && isReusable(slotIndex)) {
            return slotIndex;
        }
    }

    return -1;
}

/// Invalidate completion handlers atomically with ready-slot publication.
/// Preserve a completed old image during scene edits, but discard it on resize.
/// An outstanding submission still owns its resources until its handler drains.
inline uint64_t invalidateOutput(AsyncFrameState& state, bool discardReady) {
    std::lock_guard lock(state.publicationMutex);
    const uint64_t generation = state.generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (discardReady) {
        state.latestReadySlot.store(-1, std::memory_order_release);
        state.latestReadySampleCount.store(0, std::memory_order_relaxed);
        for (auto& slot : state.slotStates)
            slot.store(static_cast<uint8_t>(OutputSlotState::Free), std::memory_order_relaxed);
    }
    return generation;
}

inline void completeOutput(AsyncFrameState& state, int slot, uint64_t generation,
                           bool success, int samples = 0, uint64_t nanosPerSample = 0) {
    std::lock_guard lock(state.publicationMutex);
    const bool current = state.generation.load(std::memory_order_relaxed) == generation;
    if (current && success) {
        state.slotStates[slot].store(static_cast<uint8_t>(OutputSlotState::Ready), std::memory_order_relaxed);
        state.latestReadySampleCount.store(samples, std::memory_order_relaxed);
        if (nanosPerSample > 0) state.gpuNanosPerSample.store(nanosPerSample, std::memory_order_relaxed);
        state.latestReadySlot.store(slot, std::memory_order_release);
    } else {
        state.slotStates[slot].store(static_cast<uint8_t>(OutputSlotState::Free), std::memory_order_relaxed);
        if (current && !success) state.failed.store(true, std::memory_order_release);
    }
    // Publish everything before admitting another submission/buffer upload.
    state.inFlightSlot.store(-1, std::memory_order_release);
}

} // namespace atom::render::metal
