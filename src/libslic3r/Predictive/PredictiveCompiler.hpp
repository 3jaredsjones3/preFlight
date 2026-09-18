#pragma once

#include "CompilerPasses.hpp"
#include "ForwardModel.hpp"

#include <string>
#include <vector>

namespace Slic3r::Predictive {

struct CompilerConfig {
    bool enable_spectral_interlock {false};
    bool enable_nonplanar_projection {false};
    bool enable_pressure_release {true};
    bool enable_precompensation {false};
    bool require_certification {true};
    SpectralInterlockConfig interlock;
    NonplanarConfig nonplanar;
    ScheduleWeights schedule_weights;
    double pressure_release_distance_mm {0.8};
    double maximum_compensation_mm {0.15};
};

struct CompilationInput {
    BeadGraphIR bead_graph;
    GeometryFieldIR fields;
    MachineFingerprint machine;
    MaterialThermalModel material;
};

struct CompilationResult {
    BeadGraphIR planned_graph;
    std::vector<PathId> schedule;
    std::vector<TimedMove> timed_moves;
    ArtifactIR predicted_artifact;
    ValidationReport report;
    std::string gcode;
};

class PredictiveCompiler {
public:
    explicit PredictiveCompiler(CompilerConfig config);

    CompilationResult compile(const CompilationInput &input) const;

private:
    CompilerConfig m_config;
};

std::string emit_certified_gcode(const BeadGraphIR &graph,
                                 const std::vector<PathId> &schedule,
                                 const ValidationReport &certification,
                                 const MachineFingerprint &machine,
                                 bool require_certification);

} // namespace Slic3r::Predictive
