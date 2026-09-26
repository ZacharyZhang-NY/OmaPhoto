#pragma once
#include "Document/Levels.h"

// What a Levels eyedropper sets: black, gray or white point.
enum class LevelsSample { black, gray, white };
inline constexpr std::array<LevelsSample, 3> allLevelsSamples{LevelsSample::black, LevelsSample::gray, LevelsSample::white};
QString rawValue(LevelsSample sample);

// Swift's LevelsAuto: settings read from a histogram.
enum class LevelsAuto { contrast, color, neutral };
inline constexpr std::array<LevelsAuto, 3> allLevelsAutos{LevelsAuto::contrast, LevelsAuto::color, LevelsAuto::neutral};
QString rawValue(LevelsAuto mode);
LevelsSettings settings(LevelsAuto mode, const LevelsHistogram &histogram);
