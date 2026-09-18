#include "ForwardModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r::Predictive {

SparseForwardModel::SparseForwardModel(MaterialThermalModel material)
    : m_material(std::move(material))
{
}

ArtifactIR SparseForwardModel::simulate(const BeadGraphIR &graph,
                                        const std::vector<TimedMove> &moves,
                                        const MachineFingerprint &machine) const
{
    ArtifactIR artifact;
    artifact.machine_fingerprint_hash = machine.content_hash;

    for (const TimedMove &move : moves) {
        artifact.predicted_duration_s = std::max(artifact.predicted_duration_s, move.end_time_s);
        if (!move.extrusion_enabled || move.path_id == 0)
            continue;

        const BeadPath *path = graph.find(move.path_id);
        const double neighbor_radius = path != nullptr && !path->samples.empty()
                                           ? path->samples.front().width_mm * 1.5
                                           : 0.8;
        double bond_utilization = 1.0;
        double local_uncertainty = 0.02;

        for (const ArtifactCell &previous : artifact.cells) {
            if (distance(previous.position, move.end) > neighbor_radius)
                continue;
            const double age = std::max(0.0, move.end_time_s - previous.deposited_time_s);
            const double temperature = m_material.ambient_temperature_c +
                (m_material.deposition_temperature_c - m_material.ambient_temperature_c) *
                std::exp(-age / std::max(1e-9, m_material.cooling_time_constant_s));
            const double normalized_temperature =
                (temperature - m_material.glass_transition_temperature_c) /
                std::max(1e-9, m_material.weld_temperature_scale_c);
            const double candidate_bond = 1.0 / (1.0 + std::exp(-normalized_temperature));
            bond_utilization = std::min(bond_utilization, candidate_bond);
            local_uncertainty += 0.005 * std::sqrt(age);
        }

        artifact.cells.push_back({move.end, move.end_time_s,
                                  m_material.deposition_temperature_c,
                                  std::clamp(bond_utilization, 0.0, 1.0),
                                  0.0, local_uncertainty, move.path_id});
    }

    for (const BeadPath &path : graph.paths) {
        for (const ContactContract &contact : path.contacts) {
            double observed_minimum = 1.0;
            bool found = false;
            for (const ArtifactCell &cell : artifact.cells) {
                if (cell.source_path == path.id || cell.source_path == contact.other_path) {
                    observed_minimum = std::min(observed_minimum, cell.minimum_bond_utilization);
                    found = true;
                }
            }
            if (!found || observed_minimum < contact.minimum_bond_utilization ||
                observed_minimum > contact.maximum_bond_utilization) {
                artifact.certification.add(
                    Severity::Error, "artifact.contact_contract_failed",
                    "Predicted contact bond lies outside its required lower/upper bounds.", path.id);
            }
        }
    }

    return artifact;
}

} // namespace Slic3r::Predictive
