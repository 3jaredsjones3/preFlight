#include "LegacyExtrusionAdapter.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"

namespace Slic3r::Predictive {
LegacyEntitySnapshot capture_legacy_entity(const ExtrusionEntity &entity)
{
    LegacyEntitySnapshot result;
    result.can_reverse = entity.can_reverse();
    if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity)) {
        result.kind = LegacyEntityKind::Collection;
        result.no_sort = collection->no_sort;
        for (const auto *child : collection->entities) {
            result.children.push_back(child ? capture_legacy_entity(*child) : LegacyEntitySnapshot {});
            const auto role = result.children.back().attributes.role_bits;
            auto &combined = result.attributes.role_bits;
            combined = combined == 0 || combined == role ? role : legacy_role_bits(ExtrusionRole::Mixed);
        }
    } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity)) {
        result.attributes.role_bits = legacy_role_bits(loop->role());
        result.kind = LegacyEntityKind::Loop;
        result.loop_role = static_cast<int>(loop->loop_role());
        for (const auto &path : loop->paths) result.children.push_back(capture_legacy_entity(path));
    } else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(&entity)) {
        result.attributes.role_bits = legacy_role_bits(multi->role());
        result.kind = LegacyEntityKind::MultiPath;
        for (const auto &path : multi->paths) result.children.push_back(capture_legacy_entity(path));
    } else if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity)) {
        result.kind = dynamic_cast<const ExtrusionPathOriented *>(path) ? LegacyEntityKind::OrientedPath : LegacyEntityKind::Path;
        const auto &a = path->attributes();
        auto &b = result.attributes;
        b.role_bits = legacy_role_bits(a.role);
        b.width_mm = a.width; b.height_mm = a.height; b.mm3_per_mm = a.mm3_per_mm;
        b.flow_ratio = a.flow_ratio; b.maybe_self_crossing = a.maybe_self_crossing;
        b.perimeter_index = a.perimeter_index; b.feature_id = a.feature_id;
        b.region_area_mm2 = a.region_area_mm2; b.fill_pattern = a.fill_pattern;
        if (a.overhang_attributes) {
            const auto &o = *a.overhang_attributes;
            b.overhang = {o.start_distance_from_prev_layer, o.end_distance_from_prev_layer, o.proximity_to_curled_lines};
        }
        for (const auto &point : path->polyline.points)
            result.points_scaled.push_back({point.x(), point.y()});
    } else result.attributes.role_bits = legacy_role_bits(entity.role());
    return result;
}

void append_legacy_entity(const ExtrusionEntity &entity, const LegacyContext &context,
                          LegacyAnalysis &analysis, const LegacyContextResolver &resolve)
{
    append_legacy_snapshot(capture_legacy_entity(entity), context, analysis, resolve);
}
} // namespace Slic3r::Predictive
