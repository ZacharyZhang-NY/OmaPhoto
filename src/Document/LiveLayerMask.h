#pragma once
#include "IO/ProjectStore.h"
#include <vector>

namespace LiveMaskGraph {
void validate(const std::vector<ProjectLayerRecord> &layers);
}

namespace LiveMaskBaker {
// The target's pixels through its live mask; nil without pixels.
std::optional<ImportedImage> bake(const ProjectSnapshot &snapshot, QUuid target);
}
