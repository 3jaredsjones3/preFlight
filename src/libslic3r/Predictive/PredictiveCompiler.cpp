#include "PredictiveCompiler.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace Slic3r::Predictive {

namespace {

ValidationReport validate_datums(const GeometryFieldIR &fields)
{
    ValidationReport report;
    for (const FunctionalDatum &datum : fields.datums) {
        if (datum.id == 0 || !datum.protected_region.valid() ||
            datum.positional_tolerance_mm < 0.0 || datum.surface_tolerance_mm < 0.0) {
            report.add(Severity::Error, "datum.invalid_contract",
                       "Functional datum has invalid identity, bounds, or tolerance.");
        }
    }
    return report;
}

void predict_datum_contracts(ArtifactIR &artifact, const GeometryFieldIR &fields)
{
    for (const FunctionalDatum &datum : fields.datums) {
        double maximum_error = 0.0;
        double maximum_uncertainty = 0.0;
        bool observed = false;
        for (const ArtifactCell &cell : artifact.cells) {
            if (!datum.protected_region.contains(cell.position))
                continue;
            observed = true;
            maximum_error = std::max(maximum_error, cell.geometric_error_mm);
            maximum_uncertainty = std::max(maximum_uncertainty, cell.uncertainty_mm);
        }
        const bool within = observed &&
            maximum_error + maximum_uncertainty <= datum.positional_tolerance_mm;
        artifact.datum_predictions.push_back(
            {datum.id, maximum_error, maximum_uncertainty, within});
        if (!within) {
            artifact.certification.add(Severity::Error, "artifact.datum_contract_failed",
                                       "Predicted datum tolerance is not certified.");
        }
    }
}

} // namespace

PredictiveCompiler::PredictiveCompiler(CompilerConfig config)
    : m_config(std::move(config))
{
}

CompilationResult PredictiveCompiler::compile(const CompilationInput &input) const
{
    CompilationResult result;
    result.planned_graph = input.bead_graph;
    result.report.merge(validate_bead_graph(result.planned_graph));
    result.report.merge(validate_datums(input.fields));
    if (!result.report.ok())
        return result;

    if (m_config.enable_spectral_interlock) {
        result.report.merge(apply_spectral_interlock(result.planned_graph, input.fields,
                                                     input.machine, m_config.interlock));
    }
    if (m_config.enable_nonplanar_projection) {
        result.report.merge(project_nonplanar(result.planned_graph, input.fields,
                                              m_config.nonplanar));
    }
    if (m_config.enable_pressure_release) {
        plan_pressure_release(result.planned_graph, input.machine,
                              m_config.pressure_release_distance_mm);
    }

    result.schedule = schedule_paths(result.planned_graph, m_config.schedule_weights,
                                     {0.0, 0.0, 0.0}, result.report);
    result.timed_moves = parameterize_time(result.planned_graph, result.schedule,
                                           input.machine, result.report);

    if (m_config.enable_precompensation && result.report.ok()) {
        const std::vector<VibrationPrediction> vibration =
            predict_vibration(result.planned_graph, result.timed_moves, input.machine);
        apply_geometric_precompensation(result.planned_graph, vibration, input.fields,
                                        m_config.maximum_compensation_mm);
        result.schedule = schedule_paths(result.planned_graph, m_config.schedule_weights,
                                         {0.0, 0.0, 0.0}, result.report);
        result.timed_moves = parameterize_time(result.planned_graph, result.schedule,
                                               input.machine, result.report);
    }

    result.report.merge(check_swept_tool_collisions(result.planned_graph, input.machine));

    SparseForwardModel forward_model(input.material);
    result.predicted_artifact = forward_model.simulate(result.planned_graph,
                                                       result.timed_moves,
                                                       input.machine);
    predict_datum_contracts(result.predicted_artifact, input.fields);
    result.report.merge(result.predicted_artifact.certification);
    result.predicted_artifact.certification = result.report;

    result.gcode = emit_certified_gcode(result.planned_graph, result.schedule,
                                        result.report, input.machine,
                                        m_config.require_certification);
    return result;
}

std::string emit_certified_gcode(const BeadGraphIR &graph,
                                 const std::vector<PathId> &schedule,
                                 const ValidationReport &certification,
                                 const MachineFingerprint &machine,
                                 bool require_certification)
{
    if (require_certification && !certification.ok())
        return {};

    std::ostringstream output;
    output << "; generated by J-Slice predictive compiler core\n";
    output << "; machine_fingerprint_hash = " << machine.content_hash << "\n";
    output << "; certification = " << (certification.ok() ? "passed" : "bypassed") << "\n";
    output << "G90\nM83\n";
    output << std::fixed << std::setprecision(5);

    for (PathId id : schedule) {
        const BeadPath *path = graph.find(id);
        if (path == nullptr || path->samples.empty())
            continue;
        output << "; path_id=" << path->id << " pass=" << path->provenance.generating_pass << "\n";
        const BeadSample &first = path->samples.front();
        output << "G0 X" << first.position.x << " Y" << first.position.y
               << " Z" << first.position.z << "\n";
        for (std::size_t index = 1; index < path->samples.size(); ++index) {
            const BeadSample &previous = path->samples[index - 1];
            const BeadSample &sample = path->samples[index];
            const double segment_length = distance(previous.position, sample.position);
            const double area = sample.width_mm * sample.height_mm;
            const double extrusion_volume = sample.deposited_volume_mm3 > 0.0
                                                ? sample.deposited_volume_mm3
                                                : segment_length * area;
            output << (path->extrusion_enabled ? "G1" : "G0")
                   << " X" << sample.position.x << " Y" << sample.position.y
                   << " Z" << sample.position.z;
            if (path->extrusion_enabled)
                output << " E" << extrusion_volume;
            output << " F" << sample.speed_mm_s * 60.0 << "\n";
        }
    }
    return output.str();
}

} // namespace Slic3r::Predictive
