#include "LegacyExtrusionAdapter.hpp"

namespace Slic3r::Predictive {
namespace {
void append(const LegacyEntitySnapshot &source, const LegacyContext &context,
            LegacyAnalysis &out, const LegacyContextResolver &resolve,
            std::optional<std::uint64_t> parent, std::size_t child_index)
{
    LegacySourceEntity record;
    record.id = out.entities.size() + 1;
    record.parent = parent;
    record.child_index = child_index;
    record.context = context;
    if (source.island) record.context.island = source.island;
    record.data.kind = source.kind;
    record.data.can_reverse = source.can_reverse;
    record.data.no_sort = source.no_sort;
    record.data.loop_role = source.loop_role;
    record.data.island = source.island;
    record.data.attributes = source.attributes;
    record.data.points_scaled = source.points_scaled;
    const auto id = record.id;
    const bool leaf = source.kind == LegacyEntityKind::Path || source.kind == LegacyEntityKind::OrientedPath;
    if (leaf) {
        if (resolve) record.context = resolve(source.attributes, record.context);
        const auto &ctx = record.context;
        const auto &attr = source.attributes;
        BeadPath path;
        path.id = out.graph.paths.size() + 1;
        path.legacy_entity_id = id;
        path.role = legacy_path_role(attr.role_bits);
        path.provenance.source_object = ctx.object ? std::to_string(*ctx.object) : "";
        path.provenance.source_feature = ctx.source + "/entity/" + std::to_string(id);
        path.provenance.generating_pass = "legacy_analysis";
        if (ctx.layer && *ctx.layer <= std::numeric_limits<std::uint32_t>::max())
            path.provenance.source_layer = static_cast<std::uint32_t>(*ctx.layer);
        path.material_id = ctx.material.value_or("");
        path.closed = source.points_scaled.size() > 1 && source.points_scaled.front() == source.points_scaled.back();
        // Preserve zero-volume paths; never invent deposition from width*height.
        path.extrusion_enabled = attr.mm3_per_mm > 0.0;
        for (const auto &point : source.points_scaled) {
            BeadSample sample;
            sample.position = {
                (static_cast<double>(point[0]) + static_cast<double>(ctx.shift_scaled[0])) * ctx.coordinate_scale_mm,
                (static_cast<double>(point[1]) + static_cast<double>(ctx.shift_scaled[1])) * ctx.coordinate_scale_mm,
                ctx.print_z_mm.value_or(0.0)}; // placeholder for unexpanded auxiliary templates
            sample.width_mm = attr.width_mm;
            sample.height_mm = attr.height_mm;
            sample.speed_mm_s = ctx.speed_mm_s.value_or(0.0); // unknown, not a motion estimate
            if (!path.samples.empty())
                sample.deposited_volume_mm3 = distance(path.samples.back().position, sample.position) * attr.mm3_per_mm;
            path.samples.push_back(sample);
        }
        if (path.samples.size() < 2)
            out.report.add(Severity::Error, "adapter.degenerate_path", "Source path has fewer than two points; retained unchanged.", path.id);
        if (!std::isfinite(attr.mm3_per_mm) || attr.mm3_per_mm < 0.0 ||
            !std::isfinite(attr.width_mm) || !std::isfinite(attr.height_mm) ||
            attr.width_mm <= 0.0 || attr.height_mm <= 0.0)
            out.report.add(Severity::Error, "adapter.invalid_flow", "Invalid source flow; retained without repair.", path.id);
        if (path.role == PathRole::Unknown)
            out.report.add(Severity::Warning, "adapter.unknown_role", "Raw role bits retained; no coarse role mapping.", path.id);
        record.bead_path = path.id;
        out.graph.paths.push_back(std::move(path));
    }
    if (source.kind == LegacyEntityKind::Unsupported)
        out.report.add(Severity::Error, "adapter.unsupported_entity", "Unknown or null entity; snapshot is incomplete.");
    const auto child_context = record.context;
    out.entities.push_back(std::move(record));
    for (std::size_t i = 0; i < source.children.size(); ++i)
        append(source.children[i], child_context, out, resolve, id, i);
}
} // namespace

void append_legacy_snapshot(const LegacyEntitySnapshot &entity, const LegacyContext &context,
                            LegacyAnalysis &analysis, const LegacyContextResolver &resolve)
{
    analysis.graph.analysis_only = true;
    append(entity, context, analysis, resolve, std::nullopt, analysis.root_count++);
}
} // namespace Slic3r::Predictive
