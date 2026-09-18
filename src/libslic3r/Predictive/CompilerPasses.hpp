#pragma once

#include "FieldIR.hpp"
#include "ForwardModel.hpp"
#include "MachineModel.hpp"
#include "ToolpathIR.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

namespace Slic3r::Predictive {

struct StressSample {
    Vec3 position;
    Vec3 principal_direction {1.0, 0.0, 0.0};
    double normalized_magnitude {0.0};
    double confidence {1.0};
};

struct SpectralInterlockConfig {
    double amplitude_mm {0.08};
    double wavelength_mm {4.0};
    double maximum_slope {0.25};
    double nominal_speed_mm_s {40.0};
    double resonance_notch_width_hz {3.0};
};

struct NonplanarConfig {
    double maximum_slope {0.25};
    double maximum_offset_mm {0.8};
};

struct ScheduleWeights {
    double travel {1.0};
    double thermal_debt {4.0};
    double deadline {8.0};
};

struct VibrationPrediction {
    PathId path_id {0};
    double maximum_displacement_mm {0.0};
    std::vector<Vec3> predicted_errors;
};

struct VisionObservation {
    enum class Kind { None, UnderExtrusion, DetachedObject, Warping, Stringing } kind {Kind::None};
    PathId related_path {0};
    double confidence {0.0};
    double magnitude {0.0};
};

struct ClosedLoopDecision {
    enum class Action { Continue, SlowDown, AdjustFlow, ExcludeObject, Pause } action {Action::Continue};
    double bounded_adjustment {0.0};
    std::string explanation;
};

void map_stress_to_fields(GeometryFieldIR &fields, const std::vector<StressSample> &stress,
                          double minimum_density, double maximum_density);

ValidationReport apply_spectral_interlock(BeadGraphIR &graph, const GeometryFieldIR &fields,
                                          const MachineFingerprint &machine,
                                          const SpectralInterlockConfig &config);

ValidationReport project_nonplanar(BeadGraphIR &graph, const GeometryFieldIR &fields,
                                   const NonplanarConfig &config);

std::vector<PathId> schedule_paths(const BeadGraphIR &graph, const ScheduleWeights &weights,
                                   const Vec3 &initial_position, ValidationReport &report);

std::vector<TimedMove> parameterize_time(const BeadGraphIR &graph,
                                         const std::vector<PathId> &schedule,
                                         const MachineFingerprint &machine,
                                         ValidationReport &report);

std::vector<VibrationPrediction> predict_vibration(const BeadGraphIR &graph,
                                                   const std::vector<TimedMove> &moves,
                                                   const MachineFingerprint &machine);

void apply_geometric_precompensation(BeadGraphIR &graph,
                                     const std::vector<VibrationPrediction> &predictions,
                                     const GeometryFieldIR &fields,
                                     double maximum_compensation_mm);

ValidationReport check_swept_tool_collisions(const BeadGraphIR &graph,
                                             const MachineFingerprint &machine);

void plan_pressure_release(BeadGraphIR &graph, const MachineFingerprint &machine,
                           double release_distance_mm);

ClosedLoopDecision decide_closed_loop_response(const VisionObservation &observation,
                                                double confidence_threshold,
                                                double maximum_relative_adjustment);

} // namespace Slic3r::Predictive
