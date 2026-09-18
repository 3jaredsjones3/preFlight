#include "CompilerPasses.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r::Predictive {

namespace {

const FieldSample *nearest_field_sample(const GeometryFieldIR &fields, const Vec3 &point)
{
    const FieldSample *best = nullptr;
    double best_distance = std::numeric_limits<double>::infinity();
    for (const FieldSample &sample : fields.samples) {
        const double candidate_distance = squared_norm(sample.position - point);
        if (candidate_distance < best_distance) {
            best_distance = candidate_distance;
            best = &sample;
        }
    }
    return best;
}

const StressSample *nearest_stress_sample(const std::vector<StressSample> &stress, const Vec3 &point)
{
    const StressSample *best = nullptr;
    double best_distance = std::numeric_limits<double>::infinity();
    for (const StressSample &sample : stress) {
        const double candidate_distance = squared_norm(sample.position - point);
        if (candidate_distance < best_distance) {
            best_distance = candidate_distance;
            best = &sample;
        }
    }
    return best;
}

double limited_speed(const BeadSample &sample, const MachineFingerprint &machine)
{
    double speed = std::max(0.0, sample.speed_mm_s);
    speed = std::min({speed, machine.x_axis.maximum_speed_mm_s,
                     machine.y_axis.maximum_speed_mm_s});
    const double bead_area = std::max(1e-9, sample.width_mm * sample.height_mm);
    speed = std::min(speed, machine.extrusion.maximum_volumetric_flow_mm3_s / bead_area);
    return std::max(speed, machine.extrusion.minimum_extrusion_speed_mm_s);
}

bool depends_on_missing_path(const BeadGraphIR &graph, const BeadPath &path)
{
    return std::any_of(path.dependencies.begin(), path.dependencies.end(),
                       [&graph](PathId dependency) { return graph.find(dependency) == nullptr; });
}

} // namespace

ValidationReport validate_bead_graph(const BeadGraphIR &graph)
{
    ValidationReport report;
    std::unordered_set<PathId> identifiers;

    for (const BeadPath &path : graph.paths) {
        if (path.id == 0)
            report.add(Severity::Error, "path.zero_id", "Path identifiers must be nonzero.");
        if (!identifiers.insert(path.id).second)
            report.add(Severity::Error, "path.duplicate_id", "Duplicate path identifier.", path.id);
        if (path.samples.size() < 2)
            report.add(Severity::Error, "path.too_short", "A path requires at least two samples.", path.id);
        if (depends_on_missing_path(graph, path))
            report.add(Severity::Error, "path.missing_dependency",
                       "Path dependency does not exist in the graph.", path.id);
        if (path.thermal_window && !path.thermal_window->valid())
            report.add(Severity::Error, "thermal.invalid_window",
                       "Thermal window has an inverted or negative range.", path.id);

        for (const ContactContract &contact : path.contacts) {
            if (!contact.valid())
                report.add(Severity::Error, "contact.invalid_contract",
                           "Contact contract contains invalid bounds.", path.id);
            else if (graph.find(contact.other_path) == nullptr)
                report.add(Severity::Error, "contact.missing_path",
                           "Contact contract references a missing path.", path.id);
        }

        for (const BeadSample &sample : path.samples) {
            if (sample.width_mm <= 0.0 || sample.height_mm <= 0.0 || sample.speed_mm_s < 0.0)
                report.add(Severity::Error, "path.invalid_sample",
                           "Bead dimensions must be positive and speed nonnegative.", path.id);
        }
    }

    std::unordered_map<PathId, int> state;
    const auto visit = [&](const auto &self, PathId id) -> bool {
        int &current = state[id];
        if (current == 1)
            return false;
        if (current == 2)
            return true;
        current = 1;
        const BeadPath *path = graph.find(id);
        if (path != nullptr) {
            for (PathId dependency : path->dependencies)
                if (!self(self, dependency))
                    return false;
        }
        current = 2;
        return true;
    };

    for (const BeadPath &path : graph.paths) {
        if (!visit(visit, path.id)) {
            report.add(Severity::Error, "path.dependency_cycle",
                       "Path dependency graph contains a cycle.", path.id);
            break;
        }
    }

    return report;
}

void map_stress_to_fields(GeometryFieldIR &fields, const std::vector<StressSample> &stress,
                          double minimum_density, double maximum_density)
{
    minimum_density = std::clamp(minimum_density, 0.0, 1.0);
    maximum_density = std::clamp(maximum_density, minimum_density, 1.0);
    for (FieldSample &field : fields.samples) {
        const StressSample *sample = nearest_stress_sample(stress, field.position);
        if (sample == nullptr)
            continue;
        const double magnitude = std::clamp(sample->normalized_magnitude, 0.0, 1.0);
        const double confidence = std::clamp(sample->confidence, 0.0, 1.0);
        field.relative_density = minimum_density + (maximum_density - minimum_density) * magnitude;
        field.preferred_direction = normalized(sample->principal_direction);
        field.confidence = std::min(field.confidence, confidence);
    }
}

ValidationReport apply_spectral_interlock(BeadGraphIR &graph, const GeometryFieldIR &fields,
                                          const MachineFingerprint &machine,
                                          const SpectralInterlockConfig &config)
{
    ValidationReport report;
    if (config.wavelength_mm <= 0.0 || config.amplitude_mm < 0.0 || config.maximum_slope <= 0.0) {
        report.add(Severity::Error, "interlock.invalid_config", "Invalid spectral interlock configuration.");
        return report;
    }

    double wavelength = config.wavelength_mm;
    const auto excitation_frequency = [&]() {
        return config.nominal_speed_mm_s / std::max(wavelength, 1e-9);
    };
    for (const ResonanceMode &mode : machine.modes) {
        if (std::abs(excitation_frequency() - mode.frequency_hz) < config.resonance_notch_width_hz) {
            wavelength = config.nominal_speed_mm_s /
                         std::max(1.0, mode.frequency_hz + config.resonance_notch_width_hz);
            report.add(Severity::Information, "interlock.resonance_notched",
                       "Adjusted interlock wavelength away from a measured machine mode.");
        }
    }

    const double maximum_amplitude = config.maximum_slope * wavelength / (2.0 * std::numbers::pi);
    const double amplitude = std::min(config.amplitude_mm, maximum_amplitude);

    for (BeadPath &path : graph.paths) {
        if (path.role != PathRole::Perimeter && path.role != PathRole::Infill)
            continue;
        double arc_length = 0.0;
        for (std::size_t index = 1; index < path.samples.size(); ++index) {
            BeadSample &sample = path.samples[index];
            const BeadSample &previous = path.samples[index - 1];
            arc_length += distance(previous.position, sample.position);
            const FunctionalDatum *datum = fields.datum_at(sample.position);
            if (datum != nullptr && !datum->allow_interlock_deformation)
                continue;
            const Vec3 tangent = normalized(sample.position - previous.position);
            const Vec3 normal {-tangent.y, tangent.x, 0.0};
            const double offset = amplitude * std::sin(2.0 * std::numbers::pi * arc_length / wavelength);
            sample.position = sample.position + normal * offset;
        }
    }
    return report;
}

ValidationReport project_nonplanar(BeadGraphIR &graph, const GeometryFieldIR &fields,
                                   const NonplanarConfig &config)
{
    ValidationReport report;
    if (config.maximum_slope <= 0.0 || config.maximum_offset_mm < 0.0) {
        report.add(Severity::Error, "nonplanar.invalid_config", "Invalid nonplanar configuration.");
        return report;
    }

    for (BeadPath &path : graph.paths) {
        for (std::size_t index = 0; index < path.samples.size(); ++index) {
            BeadSample &sample = path.samples[index];
            const FunctionalDatum *datum = fields.datum_at(sample.position);
            if (datum != nullptr && !datum->allow_nonplanar_warp)
                continue;
            const FieldSample *field = nearest_field_sample(fields, sample.position);
            if (field == nullptr)
                continue;
            double target_z = sample.position.z + std::clamp(field->layer_coordinate - sample.position.z,
                                                             -config.maximum_offset_mm,
                                                             config.maximum_offset_mm);
            if (index > 0) {
                const Vec3 delta = sample.position - path.samples[index - 1].position;
                const double horizontal = std::hypot(delta.x, delta.y);
                const double allowed_delta = config.maximum_slope * horizontal;
                target_z = std::clamp(target_z,
                                      path.samples[index - 1].position.z - allowed_delta,
                                      path.samples[index - 1].position.z + allowed_delta);
            }
            sample.position.z = target_z;
        }
    }
    return report;
}

std::vector<PathId> schedule_paths(const BeadGraphIR &graph, const ScheduleWeights &weights,
                                   const Vec3 &initial_position, ValidationReport &report)
{
    std::vector<PathId> schedule;
    std::unordered_set<PathId> completed;
    Vec3 current_position = initial_position;

    while (schedule.size() < graph.paths.size()) {
        const BeadPath *selected = nullptr;
        double selected_score = std::numeric_limits<double>::infinity();

        for (const BeadPath &candidate : graph.paths) {
            if (completed.contains(candidate.id) || candidate.samples.empty())
                continue;
            const bool ready = std::all_of(candidate.dependencies.begin(), candidate.dependencies.end(),
                                           [&completed](PathId id) { return completed.contains(id); });
            if (!ready)
                continue;

            double score = weights.travel * distance(current_position, candidate.samples.front().position);
            if (candidate.thermal_window) {
                const double span = std::max(1.0,
                    candidate.thermal_window->maximum_neighbor_temperature_c -
                    candidate.thermal_window->minimum_neighbor_temperature_c);
                score -= weights.thermal_debt / span;
                if (std::isfinite(candidate.thermal_window->maximum_contact_age_s))
                    score -= weights.deadline / std::max(1.0, candidate.thermal_window->maximum_contact_age_s);
            }
            if (score < selected_score ||
                (std::abs(score - selected_score) < 1e-12 &&
                 (selected == nullptr || candidate.id < selected->id))) {
                selected = &candidate;
                selected_score = score;
            }
        }

        if (selected == nullptr) {
            report.add(Severity::Error, "schedule.deadlock",
                       "No schedulable path remains; dependency graph is incomplete or cyclic.");
            break;
        }
        schedule.push_back(selected->id);
        completed.insert(selected->id);
        current_position = selected->samples.back().position;
    }
    return schedule;
}

std::vector<TimedMove> parameterize_time(const BeadGraphIR &graph,
                                         const std::vector<PathId> &schedule,
                                         const MachineFingerprint &machine,
                                         ValidationReport &report)
{
    std::vector<TimedMove> moves;
    double time_s = 0.0;
    std::optional<Vec3> current;

    for (PathId id : schedule) {
        const BeadPath *path = graph.find(id);
        if (path == nullptr || path->samples.size() < 2) {
            report.add(Severity::Error, "timing.missing_path", "Scheduled path is missing or empty.", id);
            continue;
        }

        if (current && distance(*current, path->samples.front().position) > 1e-9) {
            const double travel_speed = std::min(machine.x_axis.maximum_speed_mm_s,
                                                 machine.y_axis.maximum_speed_mm_s);
            const double duration = distance(*current, path->samples.front().position) /
                                    std::max(1e-9, travel_speed);
            moves.push_back({0, *current, path->samples.front().position, time_s,
                             time_s + duration, 0.0, false});
            time_s += duration;
        }

        for (std::size_t index = 1; index < path->samples.size(); ++index) {
            const BeadSample &from = path->samples[index - 1];
            const BeadSample &to = path->samples[index];
            const double segment_length = distance(from.position, to.position);
            const double speed = limited_speed(to, machine);
            const double duration = segment_length / std::max(speed, 1e-9);
            const double volume = to.deposited_volume_mm3 > 0.0
                                      ? to.deposited_volume_mm3
                                      : segment_length * to.width_mm * to.height_mm;
            moves.push_back({path->id, from.position, to.position, time_s, time_s + duration,
                             path->extrusion_enabled ? volume : 0.0, path->extrusion_enabled});
            time_s += duration;
        }
        current = path->samples.back().position;
    }
    return moves;
}

std::vector<VibrationPrediction> predict_vibration(const BeadGraphIR &graph,
                                                   const std::vector<TimedMove> &moves,
                                                   const MachineFingerprint &machine)
{
    std::unordered_map<PathId, VibrationPrediction> predictions;
    Vec3 previous_velocity;
    bool has_previous_velocity = false;

    for (const TimedMove &move : moves) {
        if (move.path_id == 0 || !move.extrusion_enabled || graph.find(move.path_id) == nullptr)
            continue;
        const double duration = std::max(1e-9, move.end_time_s - move.start_time_s);
        const Vec3 velocity = (move.end - move.start) / duration;
        const Vec3 acceleration = has_previous_velocity ? (velocity - previous_velocity) / duration : Vec3 {};
        const double acceleration_magnitude = norm(acceleration);
        double displacement = 0.0;
        for (const ResonanceMode &mode : machine.modes) {
            const double forcing_frequency = 1.0 / duration;
            const double ratio = forcing_frequency / std::max(mode.frequency_hz, 1e-9);
            const double response = 1.0 / std::sqrt(std::pow(1.0 - ratio * ratio, 2.0) +
                                                    std::pow(2.0 * mode.damping_ratio * ratio, 2.0));
            displacement += acceleration_magnitude * mode.displacement_gain_mm_per_mm_s2 * response;
        }
        VibrationPrediction &prediction = predictions[move.path_id];
        prediction.path_id = move.path_id;
        if (prediction.predicted_errors.empty())
            prediction.predicted_errors.push_back({});
        prediction.maximum_displacement_mm = std::max(prediction.maximum_displacement_mm, displacement);
        prediction.predicted_errors.push_back(normalized(acceleration) * displacement);
        previous_velocity = velocity;
        has_previous_velocity = true;
    }

    std::vector<VibrationPrediction> result;
    result.reserve(predictions.size());
    for (auto &[id, prediction] : predictions) {
        (void) id;
        result.push_back(std::move(prediction));
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.path_id < b.path_id;
    });
    return result;
}

void apply_geometric_precompensation(BeadGraphIR &graph,
                                     const std::vector<VibrationPrediction> &predictions,
                                     const GeometryFieldIR &fields,
                                     double maximum_compensation_mm)
{
    for (const VibrationPrediction &prediction : predictions) {
        BeadPath *path = graph.find(prediction.path_id);
        if (path == nullptr)
            continue;
        for (std::size_t index = 0; index < path->samples.size() &&
                                    index < prediction.predicted_errors.size(); ++index) {
            BeadSample &sample = path->samples[index];
            const FunctionalDatum *datum = fields.datum_at(sample.position);
            if (datum != nullptr && !datum->allow_dynamic_compensation)
                continue;
            Vec3 correction = prediction.predicted_errors[index] * -1.0;
            const double magnitude = norm(correction);
            if (magnitude > maximum_compensation_mm)
                correction = correction * (maximum_compensation_mm / magnitude);
            sample.position = sample.position + correction;
        }
    }
}

ValidationReport check_swept_tool_collisions(const BeadGraphIR &graph,
                                             const MachineFingerprint &machine)
{
    ValidationReport report;
    for (const BeadPath &path : graph.paths) {
        for (const BeadSample &sample : path.samples) {
            if (!machine.build_volume.contains(sample.position)) {
                report.add(Severity::Error, "collision.outside_build_volume",
                           "A toolpath sample lies outside the declared build volume.", path.id);
                break;
            }
            if (sample.position.z - machine.tool_envelope.below_reference_mm <
                machine.build_volume.minimum.z - 1e-9) {
                report.add(Severity::Error, "collision.tool_below_bed",
                           "The swept tool envelope extends below the bed.", path.id);
                break;
            }
        }
    }
    return report;
}

void plan_pressure_release(BeadGraphIR &graph, const MachineFingerprint &machine,
                           double release_distance_mm)
{
    if (release_distance_mm <= 0.0)
        return;
    const double time_constant = std::max(1e-6, machine.extrusion.pressure_time_constant_s);
    for (BeadPath &path : graph.paths) {
        if (!path.extrusion_enabled || path.samples.size() < 2)
            continue;
        double remaining = 0.0;
        for (std::size_t index = path.samples.size() - 1; index > 0; --index) {
            const double segment_length = distance(path.samples[index].position,
                                                   path.samples[index - 1].position);
            remaining += segment_length;
            if (remaining > release_distance_mm)
                break;
            if (path.samples[index].deposited_volume_mm3 <= 0.0) {
                path.samples[index].deposited_volume_mm3 = segment_length *
                    path.samples[index].width_mm * path.samples[index].height_mm;
            }
            const double fraction = std::clamp(remaining / release_distance_mm, 0.0, 1.0);
            const double release = 1.0 - std::exp(-fraction / time_constant);
            path.samples[index].deposited_volume_mm3 *= std::clamp(release, 0.0, 1.0);
        }
    }
}

ClosedLoopDecision decide_closed_loop_response(const VisionObservation &observation,
                                                double confidence_threshold,
                                                double maximum_relative_adjustment)
{
    if (observation.confidence < confidence_threshold)
        return {ClosedLoopDecision::Action::Continue, 0.0, "Observation confidence is below policy threshold."};

    const double adjustment = std::clamp(observation.magnitude, 0.0,
                                         std::max(0.0, maximum_relative_adjustment));
    switch (observation.kind) {
    case VisionObservation::Kind::UnderExtrusion:
        return {ClosedLoopDecision::Action::AdjustFlow, adjustment,
                "Bounded flow correction for confirmed under-extrusion."};
    case VisionObservation::Kind::DetachedObject:
        return {ClosedLoopDecision::Action::ExcludeObject, 0.0,
                "Exclude a detached object without attempting physical ejection."};
    case VisionObservation::Kind::Warping:
        return {ClosedLoopDecision::Action::SlowDown, adjustment,
                "Bounded slowdown after confirmed warping."};
    case VisionObservation::Kind::Stringing:
        return {ClosedLoopDecision::Action::Continue, 0.0,
                "Stringing does not authorize an online geometry correction."};
    case VisionObservation::Kind::None:
        break;
    }
    return {ClosedLoopDecision::Action::Continue, 0.0, "No actionable observation."};
}

} // namespace Slic3r::Predictive
