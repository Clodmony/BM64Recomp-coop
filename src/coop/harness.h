#ifndef COOP_HARNESS_H
#define COOP_HARNESS_H

#include <cstdint>

// The test harness: scripted pads, the warp into a stage, probes and the
// co-op spike's switches (src/coop/harness.cpp, docs/coop/building.md).
namespace coop {
    // With BM64_COOP_INPUT=<file>, the pad state the script sets for pad
    // `pad` (0-3): returns true and fills the outputs, the way
    // recomp::get_n64_input does. Returns false without a script.
    bool scripted_input(int pad, uint16_t* buttons, float* x, float* y);

    // Whether a script runs; it then drives pads 0-3, all connected.
    bool scripted_input_active();

    // The overlay loader's hook reports each load here (the script's
    // `overlay` steps and the warp wait for loads).
    void note_overlay_load(uint32_t id);
}

#endif
