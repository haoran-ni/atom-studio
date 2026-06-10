#pragma once

#include <array>
#include <atomic>
#include <cstdint>

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

/// Lock-free bookkeeping shared between the render thread and Metal
/// command-buffer completion handlers. Held via shared_ptr so a completion
/// handler outliving the renderer stays safe.
struct AsyncFrameState {
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

/// Marks all slots free and clears the ready slot. Does NOT touch
/// inFlightSlot — the old completion handler must drain it naturally,
/// otherwise its CAS could clear a newer submission's in-flight marker.
inline void resetOutputSlots(AsyncFrameState& state) {
    state.latestReadySlot.store(-1, std::memory_order_relaxed);
    state.latestReadySampleCount.store(0, std::memory_order_relaxed);
    for (auto& slotState : state.slotStates) {
        slotState.store(static_cast<uint8_t>(OutputSlotState::Free), std::memory_order_relaxed);
    }
}

} // namespace atom::render::metal
