#pragma once

#include "MachineModel.hpp"
#include "ToolpathIR.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Slic3r::Predictive {

struct TimedMove {
    PathId path_id {0};
    Vec3 start;
    Vec3 end;
    double start_time_s {0.0};
    double end_time_s {0.0};
    double deposited_volume_mm3 {0.0};
    bool extrusion_enabled {true};
};

struct ArtifactCell {
    Vec3 position;
    double deposited_time_s {0.0};
    double temperature_c {25.0};
    double minimum_bond_utilization {1.0};
    double geometric_error_mm {0.0};
    double uncertainty_mm {0.0};
    PathId source_path {0};
};

struct DatumPrediction {
    DatumId datum_id {0};
    double predicted_maximum_error_mm {0.0};
    double uncertainty_mm {0.0};
    bool within_contract {true};
};

struct ArtifactIR {
    std::vector<ArtifactCell> cells;
    std::vector<DatumPrediction> datum_predictions;
    double predicted_duration_s {0.0};
    std::string machine_fingerprint_hash;
    ValidationReport certification;
};

struct MaterialThermalModel {
    double ambient_temperature_c {25.0};
    double deposition_temperature_c {215.0};
    double glass_transition_temperature_c {60.0};
    double cooling_time_constant_s {18.0};
    double weld_temperature_scale_c {35.0};
};

class SparseForwardModel {
public:
    explicit SparseForwardModel(MaterialThermalModel material);

    ArtifactIR simulate(const BeadGraphIR &graph, const std::vector<TimedMove> &moves,
                        const MachineFingerprint &machine) const;

private:
    MaterialThermalModel m_material;
};

} // namespace Slic3r::Predictive
