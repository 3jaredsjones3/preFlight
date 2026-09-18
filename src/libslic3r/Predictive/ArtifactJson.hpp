#pragma once
#include "LegacyExtrusionAdapter.hpp"
#include <ostream>

namespace Slic3r::Predictive {
// Versioned analysis ArtifactIR envelope. Unknown predictions are null, never
// fabricated using M0's nominal speed or uncalibrated thermal defaults.
void write_artifact_json(std::ostream &output, const LegacyAnalysis &analysis);
std::string artifact_json(const LegacyAnalysis &analysis);
} // namespace Slic3r::Predictive
