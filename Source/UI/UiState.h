#pragma once
#include "../Meters/MeterBank.h"

namespace bm::ui
{
// One consistent view of everything the panels need for a single repaint.
struct UiState
{
    bm::MeterSnapshot in, out;
    bool showOutput = true;   // Input / Output meter switch
    bool compare = false;     // delta readouts and RTA overlay
    int presetIdx = 0;
    float target = -14.0f, tolerance = 1.0f, ceiling = -1.0f;

    const bm::MeterSnapshot& shown() const noexcept { return showOutput ? out : in; }
};
} // namespace bm::ui
