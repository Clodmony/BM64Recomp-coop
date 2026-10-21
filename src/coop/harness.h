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

    // The pads the game reads, reported by recomp::get_n64_input
    // (src/game/controls.cpp) once a frame. The story intro skip (#77) watches
    // pad 0 for Z: pressed while a new file's opening cutscene (overlay 0x1A)
    // plays - so after the file was picked - the next per-frame hook goes to
    // the world map (overlay 0x21) in the running session, keeping that file.
    // Z anywhere else is the game's own button, the logos, the title's menu
    // and the attract demo included, and in an online session the skip is
    // refused, whose two games would otherwise part ways.
    void note_pad(int pad, uint16_t buttons);
}

#endif
