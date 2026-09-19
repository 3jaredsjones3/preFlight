#include "libslic3r/Predictive/ThermalSchedule.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Slic3r::Predictive;

namespace {

void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

BeadPath path(PathId id, double x, double y, PathRole role = PathRole::Perimeter)
{
    BeadPath result;
    result.id = id;
    result.role = role;
    result.source_object = 1;
    result.source_instance = 1;
    result.source_print_object = 1;
    result.material_id = "PLA";
    result.tool_id = 0;
    result.cancel_object_id = "object-1";
    result.source_order = static_cast<std::size_t>(id - 1);
    result.provenance = {"1", "feature", "legacy_analysis", 2, std::nullopt};
    result.samples = {{{x, y, 0.2}, 0.45, 0.2, 40.0, 0.0},
                      {{x + 1.0, y, 0.2}, 0.45, 0.2, 40.0, 0.09}};
    return result;
}

MachineFingerprint machine()
{
    MachineFingerprint result;
    result.content_hash = "sha256:m2-test-machine";
    result.build_volume = {{0.0, 0.0, 0.0}, {220.0, 220.0, 220.0}};
    return result;
}

ThermalModelContract model(bool calibrated)
{
    ThermalModelContract result;
    result.model_id = "m2-test-pla";
    result.machine_fingerprint_hash = "sha256:m2-test-machine";
    result.material_family = "PLA";
    result.provenance = calibrated ? ThermalModelProvenance::Measured : ThermalModelProvenance::Synthetic;
    result.calibrated = calibrated;
    result.synthetic = !calibrated;
    result.calibration_dataset_hash = calibrated ? "sha256:coupon" : "";
    result.cooling_time_constant_s = 10.0;
    result.target_contact_age_s = 2.0;
    return result;
}

void tests()
{
    BeadGraphIR graph;
    graph.paths = {path(1, 10.0, 10.0), path(2, 11.2, 10.0), path(3, 40.0, 40.0, PathRole::Bridge),
                   path(4, 12.0, 10.0, PathRole::Support)};
    graph.paths[1].dependencies = {1};
    graph.paths[3].dependencies = {2};
    ThermalScheduleOptions options;
    options.neighbor_distance_mm = 2.0;
    options.maximum_neighbor_pairs = 8;
    const auto neighbors = build_thermal_neighbor_graph(graph, model(false), options);
    require(neighbors.bounded && !neighbors.neighbors.empty(), "bounded deterministic neighbor graph");
    const auto first_json = thermal_schedule_json(propose_thermal_schedule(graph, machine(), model(false), options));
    const auto second_json = thermal_schedule_json(propose_thermal_schedule(graph, machine(), model(false), options));
    require(first_json == second_json, "thermal proposal JSON must be deterministic");

    const auto uncalibrated = propose_thermal_schedule(graph, machine(), model(false), options);
    require(!uncalibrated.recommendation_eligible && uncalibrated.proposed_order == uncalibrated.baseline_order,
            "synthetic model cannot recommend a reorder");
    require(uncalibrated.eligibility_reason.find("uncalibrated") != std::string::npos,
            "synthetic eligibility reason is explicit");
    require(uncalibrated.reorderability[2].reason.find("bridge") != std::string::npos,
            "bridge lifetime boundary is locked");
    require(uncalibrated.reorderability[3].reason.find("support") != std::string::npos,
            "support lifetime boundary is locked");
    require(!uncalibrated.neighbor_graph.neighbors.empty(), "contact finding retained");

    auto calibrated = propose_thermal_schedule(graph, machine(), model(true), options);
    require(calibrated.recommendation_eligible, "measured model with hash is eligible for analysis proposal");
    require(calibrated.baseline_order == std::vector<PathId>({1, 2, 3, 4}),
            "baseline preserves source/dependency order");
    require(calibrated.proposed_order.size() == calibrated.baseline_order.size(),
            "candidate is a permutation of baseline paths");
    require(calibrated.candidate_swaps_considered <= options.maximum_candidate_swaps,
            "candidate search is bounded");
    require(calibrated.proposed_score.total <= calibrated.baseline_score.total + 1e-9,
            "candidate never worsens the reported objective");
    ThermalScheduleOptions no_search = options;
    no_search.maximum_candidate_swaps = 0;
    const auto bounded = propose_thermal_schedule(graph, machine(), model(true), no_search);
    require(bounded.candidate_swaps_considered == 0 && bounded.proposed_order == bounded.baseline_order,
            "zero search bound rejects candidate exploration");
    auto corrupted_model = model(true);
    corrupted_model.calibration_dataset_hash = "sha256:corrupted";
    require(thermal_schedule_json(calibrated) != thermal_schedule_json(
                propose_thermal_schedule(graph, machine(), corrupted_model, options)),
            "calibration hash changes the bound proposal digest");

    auto out_of_domain = model(true);
    out_of_domain.machine_fingerprint_hash = "sha256:other";
    const auto rejected = propose_thermal_schedule(graph, machine(), out_of_domain, options);
    require(!rejected.recommendation_eligible && rejected.proposed_order == rejected.baseline_order,
            "out-of-domain model cannot recommend a schedule");
}

} // namespace

int main(int argc, char **argv)
{
    try {
        tests();
        if (argc == 2) {
            const BeadGraphIR graph;
            const MachineFingerprint machine_value = machine();
            const ThermalModelContract model_value = model(false);
            std::ofstream output(argv[1], std::ios::binary);
            output << thermal_schedule_json(propose_thermal_schedule(graph, machine_value, model_value));
            require(bool(output), "thermal JSON output failed");
        }
        std::cout << "Thermal graph, scheduling bounds, provenance and determinism checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
