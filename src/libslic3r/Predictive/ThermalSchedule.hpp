#pragma once

#include "MachineModel.hpp"
#include "ToolpathIR.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Predictive {

// M2 is an analysis contract.  A model is never treated as calibrated merely
// because it has numerical parameters; provenance and an authenticated dataset
// hash are required before a proposal can be recommendation-eligible.
enum class ThermalModelProvenance { Synthetic, Uncalibrated, Measured, Inferred };

struct ThermalModelContract {
    std::string schema_version {"js-thermal-model-1"};
    std::string model_id;
    std::string model_version {"1"};
    std::string machine_fingerprint_hash;
    std::string printer_family;
    std::string material_family;
    double nozzle_diameter_mm {0.4};
    double layer_height_mm {0.2};
    double line_width_mm {0.45};
    double ambient_temperature_c {25.0};
    double deposition_temperature_c {215.0};
    double bed_temperature_c {60.0};
    double fan_percent {0.0};
    double glass_transition_temperature_c {60.0};
    double bonding_minimum_temperature_c {95.0};
    double bonding_maximum_temperature_c {180.0};
    double cooling_time_constant_s {18.0};
    double initial_temperature_uncertainty_c {30.0};
    double model_uncertainty_c {15.0};
    double target_contact_age_s {8.0};
    std::string calibration_dataset_hash;
    ThermalModelProvenance provenance {ThermalModelProvenance::Uncalibrated};
    bool calibrated {false};
    bool synthetic {false};

    bool valid() const;
    bool applicable_to(const MachineFingerprint &machine, const std::string &material) const;
};

struct ThermalPathSummary {
    PathId path_id {0};
    std::size_t source_order {0};
    std::string source_object;
    std::string source_feature;
    std::string generating_pass;
    std::optional<std::uint32_t> source_layer;
    std::vector<PathId> dependencies;
    std::optional<std::uint32_t> tool_id;
    std::string material_id;
    std::string cancel_object_id;
    double duration_s {0.0};
    double deposition_temperature_c {0.0};
    double uncertainty_c {0.0};
};

struct ThermalNeighbor {
    PathId first_path {0};
    PathId second_path {0};
    double minimum_distance_mm {0.0};
    double contact_age_s {0.0};
    double estimated_temperature_c {0.0};
    double uncertainty_c {0.0};
    std::string contact_type {"geometric_proximity"};
    std::string confidence {"bounded"};
};

struct ThermalNeighborGraph {
    std::string schema_version {"js-thermal-neighbor-1"};
    double cell_size_mm {2.0};
    std::size_t pair_limit {4096};
    bool bounded {true};
    std::vector<ThermalPathSummary> paths;
    std::vector<ThermalNeighbor> neighbors;
    std::vector<std::string> warnings;
};

struct ThermalScore {
    double total {0.0};
    double travel {0.0};
    double thermal_debt {0.0};
    double hot_penalty {0.0};
    double cold_penalty {0.0};
    double tool_switches {0.0};
    double uncertainty {0.0};
    double legacy_deviation {0.0};
};

struct ThermalTimelineEntry {
    PathId path_id {0};
    double start_time_s {0.0};
    double end_time_s {0.0};
};

struct ThermalReorderability {
    PathId path_id {0};
    bool reorderable {false};
    std::string reason;
};

struct ThermalScheduleOptions {
    double neighbor_distance_mm {2.0};
    double grid_cell_mm {2.0};
    std::size_t maximum_neighbor_pairs {4096};
    std::size_t maximum_candidate_swaps {128};
    std::size_t maximum_passes {2};
    double travel_speed_mm_s {120.0};
    double tool_change_time_s {2.0};
};

struct ThermalScheduleProposal {
    std::string schema_version {"js-thermal-schedule-1"};
    std::string machine_fingerprint_hash;
    std::string model_id;
    std::string model_hash;
    bool analysis_only {true};
    bool recommendation_eligible {false};
    std::string eligibility_reason;
    std::vector<PathId> baseline_order;
    std::vector<PathId> proposed_order;
    std::vector<ThermalTimelineEntry> baseline_timeline;
    std::vector<ThermalTimelineEntry> proposed_timeline;
    ThermalNeighborGraph neighbor_graph;
    std::vector<ThermalReorderability> reorderability;
    ThermalScore baseline_score;
    ThermalScore proposed_score;
    std::vector<std::string> changes;
    std::vector<std::string> warnings;
    std::size_t candidate_swaps_considered {0};
    std::size_t search_passes {0};
};

ThermalNeighborGraph build_thermal_neighbor_graph(const BeadGraphIR &graph,
                                                  const ThermalModelContract &model,
                                                  const ThermalScheduleOptions &options);

ThermalScheduleProposal propose_thermal_schedule(const BeadGraphIR &graph,
                                                 const MachineFingerprint &machine,
                                                 const ThermalModelContract &model,
                                                 const ThermalScheduleOptions &options = {});

// Stable, locale-independent JSON for sidecars and review artifacts.
std::string thermal_schedule_json(const ThermalScheduleProposal &proposal);

} // namespace Slic3r::Predictive
