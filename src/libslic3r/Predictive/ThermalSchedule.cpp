#include "ThermalSchedule.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r::Predictive {
namespace {

const char *provenance_name(ThermalModelProvenance provenance)
{
    switch (provenance) {
    case ThermalModelProvenance::Synthetic: return "synthetic";
    case ThermalModelProvenance::Uncalibrated: return "uncalibrated";
    case ThermalModelProvenance::Measured: return "measured";
    case ThermalModelProvenance::Inferred: return "inferred";
    }
    return "unknown";
}

void write_string(std::ostream &out, const std::string &value)
{
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"';
}

void write_number(std::ostream &out, double value)
{
    if (!std::isfinite(value)) throw std::invalid_argument("thermal JSON cannot encode non-finite value");
    out << (value == 0.0 ? 0.0 : value);
}

void write_bool(std::ostream &out, bool value) { out << (value ? "true" : "false"); }

template<class T> void write_vector(std::ostream &out, const std::vector<T> &values,
                                    const std::function<void(std::ostream &, const T &)> &write)
{
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) out << ',';
        write(out, values[index]);
    }
    out << ']';
}

struct BoundsInfo {
    Aabb3 bounds;
    bool valid {false};
};

BoundsInfo path_bounds(const BeadPath &path)
{
    BoundsInfo result;
    if (path.samples.empty()) return result;
    result.valid = true;
    for (const BeadSample &sample : path.samples) result.bounds.include(sample.position);
    return result;
}

double axis_gap(double low_a, double high_a, double low_b, double high_b)
{
    if (high_a < low_b) return low_b - high_a;
    if (high_b < low_a) return low_a - high_b;
    return 0.0;
}

double bounds_gap(const Aabb3 &a, const Aabb3 &b)
{
    const double x = axis_gap(a.minimum.x, a.maximum.x, b.minimum.x, b.maximum.x);
    const double y = axis_gap(a.minimum.y, a.maximum.y, b.minimum.y, b.maximum.y);
    const double z = axis_gap(a.minimum.z, a.maximum.z, b.minimum.z, b.maximum.z);
    return std::sqrt(x * x + y * y + z * z);
}

double path_duration(const BeadPath &path, double fallback_speed)
{
    double duration = 0.0;
    for (std::size_t index = 1; index < path.samples.size(); ++index) {
        const BeadSample &from = path.samples[index - 1];
        const BeadSample &to = path.samples[index];
        const double speed = to.speed_mm_s > 1e-9 ? to.speed_mm_s : fallback_speed;
        duration += distance(from.position, to.position) / std::max(speed, 1e-9);
    }
    return duration;
}

std::vector<const BeadPath *> source_paths(const BeadGraphIR &graph)
{
    std::vector<const BeadPath *> paths;
    paths.reserve(graph.paths.size());
    for (const BeadPath &path : graph.paths) paths.push_back(&path);
    std::stable_sort(paths.begin(), paths.end(), [](const BeadPath *a, const BeadPath *b) {
        if (a->source_order != b->source_order) return a->source_order < b->source_order;
        return a->id < b->id;
    });
    return paths;
}

std::vector<PathId> baseline_order(const BeadGraphIR &graph, std::vector<std::string> &warnings)
{
    const auto paths = source_paths(graph);
    std::unordered_set<PathId> completed;
    std::vector<PathId> order;
    order.reserve(paths.size());
    while (order.size() < paths.size()) {
        const BeadPath *selected = nullptr;
        for (const BeadPath *path : paths) {
            if (completed.contains(path->id)) continue;
            const bool ready = std::all_of(path->dependencies.begin(), path->dependencies.end(),
                                           [&completed](PathId id) { return completed.contains(id); });
            if (ready) {
                selected = path;
                break;
            }
        }
        if (selected == nullptr) {
            warnings.push_back("baseline dependency cycle or missing dependency; source order retained");
            for (const BeadPath *path : paths)
                if (!completed.contains(path->id)) order.push_back(path->id);
            break;
        }
        order.push_back(selected->id);
        completed.insert(selected->id);
    }
    return order;
}

struct Timeline {
    std::vector<ThermalTimelineEntry> entries;
    std::unordered_map<PathId, ThermalTimelineEntry> by_id;
};

Timeline timeline_for(const BeadGraphIR &graph, const std::vector<PathId> &order,
                      const ThermalScheduleOptions &options)
{
    Timeline result;
    double time = 0.0;
    std::optional<Vec3> current;
    for (PathId id : order) {
        const BeadPath *path = graph.find(id);
        if (path == nullptr || path->samples.empty()) continue;
        const Vec3 first = path->samples.front().position;
        if (current) time += distance(*current, first) / std::max(options.travel_speed_mm_s, 1e-9);
        const double start = time;
        time += path_duration(*path, options.travel_speed_mm_s);
        const ThermalTimelineEntry entry {id, start, time};
        result.entries.push_back(entry);
        result.by_id.emplace(id, entry);
        current = path->samples.back().position;
    }
    return result;
}

bool same_group(const BeadPath &a, const BeadPath &b)
{
    return a.source_object == b.source_object && a.source_instance == b.source_instance &&
           a.source_print_object == b.source_print_object && a.material_id == b.material_id &&
           a.tool_id == b.tool_id && a.cancel_object_id == b.cancel_object_id &&
           a.provenance.source_layer == b.provenance.source_layer;
}

bool direct_dependency(const BeadPath &a, const BeadPath &b)
{
    return std::find(a.dependencies.begin(), a.dependencies.end(), b.id) != a.dependencies.end() ||
           std::find(b.dependencies.begin(), b.dependencies.end(), a.id) != b.dependencies.end();
}

ThermalReorderability reorderability(const BeadPath &path)
{
    const auto locked = [&](const char *reason) { return ThermalReorderability {path.id, false, reason}; };
    switch (path.role) {
    case PathRole::SkirtBrim: return locked("skirt/brim and first-layer boundary");
    case PathRole::Support:
    case PathRole::SupportInterface: return locked("support lifetime boundary");
    case PathRole::Bridge: return locked("bridge cooling and span order");
    case PathRole::Wipe: return locked("wipe/purge path boundary");
    case PathRole::Unknown: return locked("unknown feature role");
    default: break;
    }
    if (path.provenance.source_layer && *path.provenance.source_layer == 0)
        return locked("first-layer datum and adhesion boundary");
    if (path.samples.size() < 2) return locked("empty or degenerate path");
    return {path.id, true, "dependency-equivalent within object/material/tool boundary"};
}

double thermal_temperature(const ThermalModelContract &model, double age_s)
{
    const double tau = std::max(model.cooling_time_constant_s, 1e-9);
    return model.ambient_temperature_c +
           (model.deposition_temperature_c - model.ambient_temperature_c) * std::exp(-std::max(age_s, 0.0) / tau);
}

ThermalScore score_order(const BeadGraphIR &graph, const ThermalNeighborGraph &neighbors,
                         const std::vector<PathId> &order, const ThermalModelContract &model,
                         const ThermalScheduleOptions &options, const std::vector<PathId> &baseline)
{
    ThermalScore score;
    const Timeline timeline = timeline_for(graph, order, options);
    std::optional<Vec3> current;
    for (PathId id : order) {
        const BeadPath *path = graph.find(id);
        if (path == nullptr || path->samples.empty()) continue;
        if (current) score.travel += distance(*current, path->samples.front().position);
        current = path->samples.back().position;
    }
    for (const ThermalNeighbor &neighbor : neighbors.neighbors) {
        const auto first = timeline.by_id.find(neighbor.first_path);
        const auto second = timeline.by_id.find(neighbor.second_path);
        if (first == timeline.by_id.end() || second == timeline.by_id.end()) continue;
        const double age = std::abs(second->second.start_time_s - first->second.end_time_s);
        const double temperature = thermal_temperature(model, age);
        const double uncertainty = model.initial_temperature_uncertainty_c + model.model_uncertainty_c;
        score.uncertainty += uncertainty;
        if (temperature < model.bonding_minimum_temperature_c)
            score.cold_penalty += model.bonding_minimum_temperature_c - temperature;
        if (temperature > model.bonding_maximum_temperature_c)
            score.hot_penalty += temperature - model.bonding_maximum_temperature_c;
        score.thermal_debt += std::abs(age - model.target_contact_age_s);
    }
    for (std::size_t index = 1; index < order.size(); ++index) {
        const BeadPath *previous = graph.find(order[index - 1]);
        const BeadPath *current_path = graph.find(order[index]);
        if (previous != nullptr && current_path != nullptr && previous->tool_id != current_path->tool_id)
            score.tool_switches += 1.0;
    }
    for (std::size_t index = 0; index < order.size() && index < baseline.size(); ++index)
        if (order[index] != baseline[index]) score.legacy_deviation += 1.0;
    score.total = score.travel + score.thermal_debt * 0.25 + score.hot_penalty * 2.0 + score.cold_penalty * 2.0 +
                  score.tool_switches * options.tool_change_time_s + score.uncertainty * 0.01 +
                  score.legacy_deviation * 0.1;
    return score;
}

std::string model_digest(const ThermalModelContract &model)
{
    std::ostringstream canonical;
    canonical.imbue(std::locale::classic());
    canonical << std::setprecision(std::numeric_limits<double>::max_digits10)
              << model.schema_version << '|' << model.model_id << '|' << model.model_version << '|'
              << model.machine_fingerprint_hash << '|' << model.material_family << '|'
              << model.nozzle_diameter_mm << '|' << model.layer_height_mm << '|'
              << model.line_width_mm << '|' << model.ambient_temperature_c << '|'
              << model.deposition_temperature_c << '|' << model.bed_temperature_c << '|'
              << model.fan_percent << '|' << model.cooling_time_constant_s << '|'
              << model.calibration_dataset_hash << '|' << provenance_name(model.provenance);
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : canonical.str()) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    std::ostringstream result;
    result << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return result.str();
}

void write_score(std::ostream &out, const ThermalScore &score)
{
    out << "{\"total\":"; write_number(out, score.total);
    out << ",\"travel\":"; write_number(out, score.travel);
    out << ",\"thermal_debt\":"; write_number(out, score.thermal_debt);
    out << ",\"hot_penalty\":"; write_number(out, score.hot_penalty);
    out << ",\"cold_penalty\":"; write_number(out, score.cold_penalty);
    out << ",\"tool_switches\":"; write_number(out, score.tool_switches);
    out << ",\"uncertainty\":"; write_number(out, score.uncertainty);
    out << ",\"legacy_deviation\":"; write_number(out, score.legacy_deviation); out << '}';
}

} // namespace

bool ThermalModelContract::valid() const
{
    return !schema_version.empty() && !model_id.empty() && nozzle_diameter_mm > 0.0 &&
           layer_height_mm > 0.0 && line_width_mm > 0.0 && fan_percent >= 0.0 && fan_percent <= 100.0 &&
           cooling_time_constant_s > 0.0 && bonding_minimum_temperature_c <= bonding_maximum_temperature_c &&
           ambient_temperature_c <= deposition_temperature_c && target_contact_age_s >= 0.0 &&
           std::isfinite(ambient_temperature_c) && std::isfinite(deposition_temperature_c) &&
           std::isfinite(cooling_time_constant_s) && std::isfinite(model_uncertainty_c) &&
           std::isfinite(initial_temperature_uncertainty_c);
}

bool ThermalModelContract::applicable_to(const MachineFingerprint &machine, const std::string &material) const
{
    return valid() && (machine_fingerprint_hash.empty() || machine_fingerprint_hash == machine.content_hash) &&
           (material_family.empty() || material_family == material);
}

ThermalNeighborGraph build_thermal_neighbor_graph(const BeadGraphIR &graph,
                                                  const ThermalModelContract &model,
                                                  const ThermalScheduleOptions &options)
{
    ThermalNeighborGraph result;
    result.cell_size_mm = std::max(options.grid_cell_mm, 1e-6);
    result.pair_limit = options.maximum_neighbor_pairs;
    const auto paths = source_paths(graph);
    const double fallback_speed = 1.0;
    for (const BeadPath *path : paths) {
        const bool unknown_speed = std::any_of(path->samples.begin(), path->samples.end(),
                                               [](const BeadSample &sample) { return sample.speed_mm_s <= 0.0; });
        result.paths.push_back({path->id, path->source_order, path->provenance.source_object,
                                path->provenance.source_feature, path->provenance.generating_pass,
                                path->provenance.source_layer, path->dependencies, path->tool_id,
                                path->material_id, path->cancel_object_id, path_duration(*path, fallback_speed),
                                model.deposition_temperature_c, unknown_speed ? 30.0 : model.model_uncertainty_c});
    }
    using GridKey = std::array<int, 3>;
    std::map<GridKey, std::vector<const BeadPath *>> grid;
    std::unordered_map<PathId, BoundsInfo> bounds;
    for (const BeadPath *path : paths) {
        const BoundsInfo info = path_bounds(*path);
        if (!info.valid) continue;
        bounds.emplace(path->id, info);
        const auto cell = [result](double value) {
            return static_cast<int>(std::floor(value / result.cell_size_mm));
        };
        grid[{cell(info.bounds.minimum.x), cell(info.bounds.minimum.y), cell(info.bounds.minimum.z)}].push_back(path);
    }
    std::set<std::pair<PathId, PathId>> seen;
    for (const auto &[key, bucket] : grid) {
        for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
            const GridKey neighbor_key {key[0] + dx, key[1] + dy, key[2] + dz};
            const auto found = grid.find(neighbor_key);
            if (found == grid.end()) continue;
            for (const BeadPath *a : bucket) for (const BeadPath *b : found->second) {
                if (a->id >= b->id) continue;
                const auto ids = std::make_pair(a->id, b->id);
                if (!seen.insert(ids).second) continue;
                const double gap = bounds_gap(bounds.at(a->id).bounds, bounds.at(b->id).bounds);
                if (gap > options.neighbor_distance_mm) continue;
                if (result.neighbors.size() >= options.maximum_neighbor_pairs) {
                    result.warnings.push_back("neighbor pair limit reached; graph is conservatively truncated");
                    return result;
                }
                result.neighbors.push_back({a->id, b->id, gap, 0.0, model.deposition_temperature_c,
                                            model.model_uncertainty_c, "geometric_proximity",
                                            gap <= std::min(a->samples.front().width_mm, b->samples.front().width_mm)
                                                ? "high" : "bounded"});
            }
        }
    }
    std::sort(result.neighbors.begin(), result.neighbors.end(), [](const ThermalNeighbor &a, const ThermalNeighbor &b) {
        return std::tie(a.first_path, a.second_path) < std::tie(b.first_path, b.second_path);
    });
    std::vector<std::string> timing_warnings;
    const Timeline timeline = timeline_for(graph, baseline_order(graph, timing_warnings), options);
    for (ThermalNeighbor &neighbor : result.neighbors) {
        const auto first = timeline.by_id.find(neighbor.first_path);
        const auto second = timeline.by_id.find(neighbor.second_path);
        if (first != timeline.by_id.end() && second != timeline.by_id.end()) {
            neighbor.contact_age_s = std::abs(second->second.start_time_s - first->second.end_time_s);
            neighbor.estimated_temperature_c = thermal_temperature(model, neighbor.contact_age_s);
        }
    }
    result.warnings.insert(result.warnings.end(), timing_warnings.begin(), timing_warnings.end());
    return result;
}

ThermalScheduleProposal propose_thermal_schedule(const BeadGraphIR &graph,
                                                 const MachineFingerprint &machine,
                                                 const ThermalModelContract &model,
                                                 const ThermalScheduleOptions &options)
{
    ThermalScheduleProposal result;
    result.machine_fingerprint_hash = machine.content_hash;
    result.model_id = model.model_id;
    result.model_hash = model_digest(model);
    result.neighbor_graph = build_thermal_neighbor_graph(graph, model, options);
    result.baseline_order = baseline_order(graph, result.warnings);
    result.proposed_order = result.baseline_order;
    result.baseline_timeline = timeline_for(graph, result.baseline_order, options).entries;
    const auto paths = source_paths(graph);
    std::unordered_map<PathId, ThermalReorderability> rules;
    for (const BeadPath *path : paths) rules.emplace(path->id, reorderability(*path));
    for (PathId id : result.baseline_order) result.reorderability.push_back(rules.at(id));
    result.baseline_score = score_order(graph, result.neighbor_graph, result.baseline_order, model, options,
                                        result.baseline_order);
    result.proposed_score = result.baseline_score;

    const std::string material = graph.paths.empty() ? std::string {} : graph.paths.front().material_id;
    if (!model.valid()) {
        result.eligibility_reason = "thermal model contract is invalid";
        result.warnings.push_back("invalid thermal model; no candidate schedule produced");
    } else if (!model.applicable_to(machine, material)) {
        result.eligibility_reason = "thermal model is outside machine/material domain";
        result.warnings.push_back("out-of-domain model; schedule remains baseline");
    } else if (!model.calibrated || model.provenance != ThermalModelProvenance::Measured ||
               model.calibration_dataset_hash.empty()) {
        result.eligibility_reason = "model is synthetic or uncalibrated; physical recommendation is disabled";
        result.warnings.push_back("analysis-only synthetic/uncalibrated model; no printer-changing recommendation");
    } else {
        result.recommendation_eligible = true;
        result.eligibility_reason = "measured model is in domain and carries a calibration dataset hash";
    }

    if (result.recommendation_eligible) {
        for (std::size_t pass = 0; pass < options.maximum_passes; ++pass) {
            bool changed = false;
            ++result.search_passes;
            for (std::size_t index = 0; index + 1 < result.proposed_order.size() &&
                                         result.candidate_swaps_considered < options.maximum_candidate_swaps; ++index) {
                const BeadPath *a = graph.find(result.proposed_order[index]);
                const BeadPath *b = graph.find(result.proposed_order[index + 1]);
                if (a == nullptr || b == nullptr || !rules.at(a->id).reorderable || !rules.at(b->id).reorderable ||
                    !same_group(*a, *b) || direct_dependency(*a, *b)) continue;
                ++result.candidate_swaps_considered;
                std::swap(result.proposed_order[index], result.proposed_order[index + 1]);
                const ThermalScore candidate = score_order(graph, result.neighbor_graph, result.proposed_order,
                                                           model, options, result.baseline_order);
                if (candidate.total + 1e-12 < result.proposed_score.total) {
                    result.proposed_score = candidate;
                    changed = true;
                } else {
                    std::swap(result.proposed_order[index], result.proposed_order[index + 1]);
                }
            }
            if (!changed) break;
        }
    }
    result.proposed_timeline = timeline_for(graph, result.proposed_order, options).entries;
    if (result.proposed_order != result.baseline_order) result.changes.push_back("reordered dependency-equivalent paths");
    else result.changes.push_back("no path order change");
    return result;
}

std::string thermal_schedule_json(const ThermalScheduleProposal &proposal)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\"schema_version\":"; write_string(out, proposal.schema_version);
    out << ",\"machine_fingerprint_hash\":"; write_string(out, proposal.machine_fingerprint_hash);
    out << ",\"model_id\":"; write_string(out, proposal.model_id);
    out << ",\"model_hash\":"; write_string(out, proposal.model_hash);
    out << ",\"analysis_only\":"; write_bool(out, proposal.analysis_only);
    out << ",\"recommendation_eligible\":"; write_bool(out, proposal.recommendation_eligible);
    out << ",\"eligibility_reason\":"; write_string(out, proposal.eligibility_reason);
    const auto write_ids = [&out](const std::vector<PathId> &ids) {
        out << '['; for (std::size_t i = 0; i < ids.size(); ++i) { if (i) out << ','; out << ids[i]; } out << ']';
    };
    out << ",\"baseline_order\":"; write_ids(proposal.baseline_order);
    out << ",\"proposed_order\":"; write_ids(proposal.proposed_order);
    const auto write_timeline = [&out](const std::vector<ThermalTimelineEntry> &entries) {
        out << '['; for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i) out << ','; out << "{\"path_id\":" << entries[i].path_id << ",\"start_time_s\":";
            write_number(out, entries[i].start_time_s); out << ",\"end_time_s\":"; write_number(out, entries[i].end_time_s); out << '}';
        } out << ']';
    };
    out << ",\"baseline_timeline\":"; write_timeline(proposal.baseline_timeline);
    out << ",\"proposed_timeline\":"; write_timeline(proposal.proposed_timeline);
    out << ",\"changes\":"; write_vector<std::string>(out, proposal.changes, [](std::ostream &s, const std::string &v) { write_string(s, v); });
    out << ",\"warnings\":"; write_vector<std::string>(out, proposal.warnings, [](std::ostream &s, const std::string &v) { write_string(s, v); });
    out << ",\"candidate_swaps_considered\":" << proposal.candidate_swaps_considered;
    out << ",\"search_passes\":" << proposal.search_passes;
    out << ",\"baseline_score\":"; write_score(out, proposal.baseline_score);
    out << ",\"proposed_score\":"; write_score(out, proposal.proposed_score);
    out << ",\"reorderability\":[";
    for (std::size_t i = 0; i < proposal.reorderability.size(); ++i) {
        if (i) out << ','; const auto &entry = proposal.reorderability[i];
        out << "{\"path_id\":" << entry.path_id << ",\"reorderable\":"; write_bool(out, entry.reorderable);
        out << ",\"reason\":"; write_string(out, entry.reason); out << '}';
    }
    out << "],\"neighbor_graph\":{\"schema_version\":"; write_string(out, proposal.neighbor_graph.schema_version);
    out << ",\"cell_size_mm\":"; write_number(out, proposal.neighbor_graph.cell_size_mm);
    out << ",\"pair_limit\":" << proposal.neighbor_graph.pair_limit;
    out << ",\"bounded\":"; write_bool(out, proposal.neighbor_graph.bounded);
    out << ",\"paths\":[";
    for (std::size_t i = 0; i < proposal.neighbor_graph.paths.size(); ++i) {
        if (i) out << ','; const auto &p = proposal.neighbor_graph.paths[i];
        out << "{\"path_id\":" << p.path_id << ",\"source_order\":" << p.source_order
            << ",\"source_object\":"; write_string(out, p.source_object);
        out << ",\"source_feature\":"; write_string(out, p.source_feature);
        out << ",\"generating_pass\":"; write_string(out, p.generating_pass);
        out << ",\"source_layer\":";
        if (p.source_layer) out << *p.source_layer; else out << "null";
        out << ",\"dependencies\":[";
        for (std::size_t j = 0; j < p.dependencies.size(); ++j) {
            if (j) out << ','; out << p.dependencies[j];
        }
        out << "],\"tool_id\":";
        if (p.tool_id) out << *p.tool_id; else out << "null";
        out << ",\"material_id\":"; write_string(out, p.material_id);
        out << ",\"cancel_object_id\":"; write_string(out, p.cancel_object_id);
        out << ",\"duration_s\":"; write_number(out, p.duration_s);
        out << ",\"deposition_temperature_c\":"; write_number(out, p.deposition_temperature_c);
        out << ",\"uncertainty_c\":"; write_number(out, p.uncertainty_c); out << '}';
    }
    out << "],\"neighbors\":[";
    for (std::size_t i = 0; i < proposal.neighbor_graph.neighbors.size(); ++i) {
        if (i) out << ','; const auto &n = proposal.neighbor_graph.neighbors[i];
        out << "{\"first_path\":" << n.first_path << ",\"second_path\":" << n.second_path << ",\"minimum_distance_mm\":";
        write_number(out, n.minimum_distance_mm); out << ",\"contact_age_s\":"; write_number(out, n.contact_age_s);
        out << ",\"estimated_temperature_c\":"; write_number(out, n.estimated_temperature_c);
        out << ",\"uncertainty_c\":"; write_number(out, n.uncertainty_c);
        out << ",\"contact_type\":"; write_string(out, n.contact_type);
        out << ",\"confidence\":"; write_string(out, n.confidence); out << '}';
    }
    out << "],\"warnings\":[";
    for (std::size_t i = 0; i < proposal.neighbor_graph.warnings.size(); ++i) {
        if (i) out << ','; write_string(out, proposal.neighbor_graph.warnings[i]);
    }
    out << "]}}\n";
    return out.str();
}

} // namespace Slic3r::Predictive
