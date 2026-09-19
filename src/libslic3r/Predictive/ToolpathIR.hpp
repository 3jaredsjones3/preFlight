#pragma once

#include "Types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Slic3r::Predictive {

enum class PathRole {
    ExternalPerimeter,
    Perimeter,
    Infill,
    SolidInfill,
    TopSurface,
    Bridge,
    Support,
    SupportInterface,
    SkirtBrim,
    Travel,
    Calibration,
    GapFill,
    Ironing,
    Wipe,
    Unknown
};

struct ThermalWindow {
    double minimum_neighbor_temperature_c {0.0};
    double maximum_neighbor_temperature_c {300.0};
    double maximum_contact_age_s {std::numeric_limits<double>::infinity()};

    bool valid() const
    {
        return minimum_neighbor_temperature_c <= maximum_neighbor_temperature_c &&
               maximum_contact_age_s >= 0.0;
    }
};

struct BeadSample {
    Vec3 position;
    double width_mm {0.4};
    double height_mm {0.2};
    double speed_mm_s {40.0};
    double deposited_volume_mm3 {0.0};
};

struct ContactContract {
    PathId other_path {0};

    enum class Intent {
        StructuralBond,
        TemporaryBrace,
        DesignedFracture,
        NonBondingContact
    } intent {Intent::StructuralBond};

    double minimum_bond_utilization {0.0};
    double maximum_bond_utilization {1.0};
    double minimum_contact_area_mm2 {0.0};
    double scar_budget_mm {0.0};
    bool removal_access_required {false};

    bool valid() const
    {
        return other_path != 0 && minimum_bond_utilization >= 0.0 &&
               maximum_bond_utilization <= 1.0 &&
               minimum_bond_utilization <= maximum_bond_utilization &&
               minimum_contact_area_mm2 >= 0.0 && scar_budget_mm >= 0.0;
    }
};

struct BeadPath {
    PathId id {0};
    PathRole role {PathRole::Infill};
    std::vector<BeadSample> samples;
    std::vector<PathId> dependencies;
    std::vector<ContactContract> contacts;
    std::optional<ThermalWindow> thermal_window;
    Provenance provenance;
    std::string material_id;
    // Stable source identity captured by M1.  These fields are metadata only;
    // they never participate in legacy G-code lowering.
    std::optional<std::uint64_t> source_object;
    std::optional<std::uint64_t> source_instance;
    std::optional<std::uint64_t> source_print_object;
    std::optional<std::uint32_t> tool_id;
    std::string cancel_object_id;
    std::size_t source_order {0};
    bool closed {false};
    bool extrusion_enabled {true};
    // M1 source entity record; absent for paths made by the research compiler.
    std::optional<std::uint64_t> legacy_entity_id;

    Aabb3 bounds() const
    {
        Aabb3 result;
        for (const BeadSample &sample : samples)
            result.include(sample.position);
        return result;
    }

    double length_mm() const
    {
        double result = 0.0;
        for (std::size_t index = 1; index < samples.size(); ++index)
            result += distance(samples[index - 1].position, samples[index].position);
        return result;
    }
};

struct BeadGraphIR {
    std::vector<BeadPath> paths;
    bool analysis_only {false}; // source inventory with unresolved emission state

    const BeadPath *find(PathId id) const
    {
        for (const BeadPath &path : paths)
            if (path.id == id)
                return &path;
        return nullptr;
    }

    BeadPath *find(PathId id)
    {
        for (BeadPath &path : paths)
            if (path.id == id)
                return &path;
        return nullptr;
    }
};

ValidationReport validate_bead_graph(const BeadGraphIR &graph);

} // namespace Slic3r::Predictive
