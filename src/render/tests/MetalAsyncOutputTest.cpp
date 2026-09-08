#include "metal/MetalAsyncOutput.h"
#include <iostream>
#include <thread>
using namespace atom::render::metal;
int main() {
    AsyncFrameState state;
    for (int i=0; i<2000; ++i) {
        const auto generation=state.generation.load();
        state.inFlightSlot.store(0);
        std::thread completion([&] { completeOutput(state,0,generation,true,10); });
        const auto next=invalidateOutput(state,true);
        completion.join();
        if (state.generation.load()!=next || state.latestReadySlot.load()!=-1 || state.inFlightSlot.load()!=-1) {
            std::cerr << "Old generation published after reset\n"; return 1;
        }
    }
    state.inFlightSlot.store(1);
    completeOutput(state,1,state.generation.load(),true,20,100);
    if (state.latestReadySlot.load()!=1 || state.latestReadySampleCount.load()!=20 || acquireOutputSlot(state,1)==1) return 1;
    invalidateOutput(state,false);
    if (state.latestReadySlot.load()!=1) return 1; // Preserve last valid image during scene preparation.
    state.inFlightSlot.store(2);
    completeOutput(state,2,state.generation.load(),false);
    return state.failed.load() && state.inFlightSlot.load()==-1 ? 0 : 1;
}
