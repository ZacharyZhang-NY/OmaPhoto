#pragma once

// Clone Stamp's options-bar settings.
struct CloneSettings {
    // The source follows the brush, keeping its offset between strokes.
    bool aligned = true;
    // Every visible layer as shown, not the active one alone.
    bool sampleAllLayers = false;
    friend bool operator==(const CloneSettings &, const CloneSettings &) = default;
};
