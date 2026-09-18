#pragma once

#include "ToolpathIR.hpp"
#include "libslic3r/ExtrusionRole.hpp"

#include <array>
#include <functional>

namespace Slic3r { class ExtrusionEntity; class Print; }

namespace Slic3r::Predictive {

// M1 contract: reads generated trees; changes only the analysis result; protects
// every source attribute, child order and the entire legacy emission branch.
// This is a storage-order snapshot, NOT an emitted-motion schedule.
struct LegacyContext {
    std::optional<std::uint64_t> object, instance, print_object, layer, region, island;
    std::optional<std::uint32_t> tool; // zero based, configured (not wipe override)
    std::optional<std::string> material, cancel_object, generator;
    std::optional<double> speed_mm_s; // absent until actually resolved by caller
    std::string source;
    std::optional<double> print_z_mm; // absent for auxiliary generator templates
    std::array<std::int64_t, 2> shift_scaled {0, 0};
    double coordinate_scale_mm {0.000001};
};

struct LegacyAttributes {
    std::uint16_t role_bits {0};
    double width_mm {-1.0}, height_mm {-1.0}, mm3_per_mm {-1.0}, flow_ratio {1.0};
    bool maybe_self_crossing {false};
    std::optional<std::array<double, 3>> overhang;
    std::optional<std::uint16_t> perimeter_index, feature_id;
    double region_area_mm2 {0.0};
    int fill_pattern {-1};
};

enum class LegacyEntityKind { Collection, Loop, MultiPath, Path, OrientedPath, Unsupported };

// Dependency-free snapshot boundary. Native capture is implemented against the
// real preFlight types, not a substitute definition of ExtrusionEntity.
struct LegacyEntitySnapshot {
    LegacyEntityKind kind {LegacyEntityKind::Unsupported};
    bool can_reverse {false}, no_sort {false};
    std::optional<int> loop_role;
    std::optional<std::uint64_t> island;
    LegacyAttributes attributes;
    std::vector<std::array<std::int64_t, 2>> points_scaled;
    std::vector<LegacyEntitySnapshot> children;
};

struct LegacySourceEntity {
    std::uint64_t id {0};
    std::optional<std::uint64_t> parent;
    std::size_t child_index {0};
    LegacyContext context;
    LegacyEntitySnapshot data; // children stored separately in preorder
    std::optional<PathId> bead_path;
};

struct LegacyAnalysis {
    BeadGraphIR graph;
    std::vector<LegacySourceEntity> entities;
    std::size_t root_count {0};
    ValidationReport report;
    std::vector<std::string> unmodeled_downstream_passes {
        "legacy ordering, tool/wipe overrides, speed/acceleration selection",
        "seam placement, scarf seams, loop clipping, path smoothing and arc fitting",
        "travel, retraction, wipe motions and generated wipe-tower G-code",
        "spiral vase (when enabled)", "pressure equalization (when enabled)",
        "cooling and fan control", "find/replace (when enabled)",
        "custom G-code, Python preprocessing and external postprocessing (when enabled)"
    };
};

std::uint16_t legacy_role_bits(ExtrusionRole role);
PathRole legacy_path_role(std::uint16_t bits);
// Resolver may supply per-leaf configured tool/material; it must not mutate source.
using LegacyContextResolver = std::function<LegacyContext(const LegacyAttributes &, const LegacyContext &)>;
void append_legacy_snapshot(const LegacyEntitySnapshot &entity, const LegacyContext &context,
                            LegacyAnalysis &analysis, const LegacyContextResolver &resolve = {});
LegacyEntitySnapshot capture_legacy_entity(const ExtrusionEntity &entity);
void append_legacy_entity(const ExtrusionEntity &entity, const LegacyContext &context,
                          LegacyAnalysis &analysis, const LegacyContextResolver &resolve = {});
LegacyAnalysis analyze_legacy_print(const Print &print);

} // namespace Slic3r::Predictive
