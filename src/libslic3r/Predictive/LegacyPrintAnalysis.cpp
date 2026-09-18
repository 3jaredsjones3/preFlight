#include "LegacyExtrusionAdapter.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r::Predictive {
LegacyAnalysis analyze_legacy_print(const Print &print)
{
    LegacyAnalysis out;
    out.report.add(Severity::Warning, "analysis.storage_order",
        "Generated entity inventory, not final emission order. Duration, thermal and bond predictions are unavailable.");
    out.report.add(Severity::Warning, "analysis.unresolved_emitter_state",
        "Speed, seam, wipe overrides and firmware cancel labels are resolved downstream. Model object/instance indices are retained.");
    out.report.add(Severity::Warning, "analysis.auxiliary_templates",
        "Skirt/brim are generator templates; layer repetitions, selected tools and wipe-tower program are not expanded.");

    const auto material_for = [&print](LegacyContext &context, unsigned int one_based_tool) {
        if (one_based_tool == 0) return; // zero means current tool for support
        context.tool = one_based_tool - 1;
        const auto &materials = print.config().filament_type.values;
        if (*context.tool < materials.size()) context.material = materials[*context.tool];
    };
    std::uint64_t print_object_index = 0;
    for (const PrintObject *object : print.objects()) {
        for (const auto &instance : object->instances()) {
            LegacyContext base;
            const auto *model_object = instance.model_instance->get_object();
            const auto &objects = model_object->get_model()->objects;
            base.object = static_cast<std::uint64_t>(std::find(objects.begin(), objects.end(), model_object) - objects.begin());
            base.instance = static_cast<std::uint64_t>(std::find(model_object->instances.begin(), model_object->instances.end(), instance.model_instance) - model_object->instances.begin());
            base.print_object = print_object_index;
            base.shift_scaled = {instance.shift.x(), instance.shift.y()};
            base.coordinate_scale_mm = SCALING_FACTOR;
            for (const Layer *layer : object->layers()) {
                base.layer = layer->id();
                base.print_z_mm = layer->print_z + print.config().z_offset.value;
                std::uint32_t region_index = 0;
                for (const LayerRegion *region : layer->regions()) {
                    LegacyContext context = base;
                    context.region = static_cast<std::uint64_t>(region->region().print_region_id());
                    context.generator = object->config().perimeter_generator.serialize();
                    const LegacyContextResolver resolve = [&](const LegacyAttributes &attributes, const LegacyContext &input) {
                        auto resolved = input;
                        const auto role = legacy_path_role(attributes.role_bits);
                        const auto &config = region->region().config();
                        const bool infill = (attributes.role_bits & (1u << static_cast<unsigned int>(ExtrusionRoleModifier::Infill))) != 0;
                        const bool interlocking = (attributes.role_bits & (1u << static_cast<unsigned int>(ExtrusionRoleModifier::Interlocking))) != 0;
                        const int tool = interlocking ? config.interlocking_perimeter_extruder.value :
                            infill ? (role == PathRole::Infill ? config.infill_extruder.value : config.solid_infill_extruder.value) : config.perimeter_extruder.value;
                        if (tool > 0) material_for(resolved, static_cast<unsigned int>(tool));
                        return resolved;
                    };
                    for (int group = 0; group < 3; ++group) {
                        context.source = group == 0 ? "perimeters" : group == 1 ? "fills" : "interlocking_gap_fills";
                        const auto &collection = group == 0 ? region->perimeters() : group == 1 ? region->fills() : region->interlocking_gap_fills();
                        auto snapshot = capture_legacy_entity(collection);
                        std::uint64_t island_index = 0;
                        for (const auto &slice : layer->lslices_ex) {
                            for (const auto &island : slice.islands) {
                                const auto assign = [&](const LayerExtrusionRange &range) {
                                    if (range.region() == region_index)
                                        for (const auto index : range)
                                            if (index < snapshot.children.size()) snapshot.children[index].island = island_index;
                                };
                                if (group == 0) assign(island.perimeters);
                                if (group == 1) for (const auto &range : island.fills) assign(range);
                                ++island_index;
                            }
                        }
                        append_legacy_snapshot(snapshot, context, out, resolve);
                    }
                    ++region_index;
                }
            }
            for (const SupportLayer *layer : object->support_layers()) {
                auto context = base;
                context.layer = layer->id();
                context.print_z_mm = layer->print_z + print.config().z_offset.value;
                context.source = "support";
                append_legacy_entity(layer->support_fills, context, out,
                    [&](const LegacyAttributes &attributes, const LegacyContext &input) {
                        auto resolved = input;
                        const auto &config = object->config();
                        const int tool = legacy_path_role(attributes.role_bits) == PathRole::SupportInterface ?
                            config.support_material_interface_extruder.value : config.support_material_extruder.value;
                        if (tool > 0) material_for(resolved, static_cast<unsigned int>(tool));
                        return resolved;
                    });
            }
        }
        ++print_object_index;
    }
    LegacyContext auxiliary;
    auxiliary.coordinate_scale_mm = SCALING_FACTOR;
    auxiliary.source = "skirt_template";
    append_legacy_entity(print.skirt(), auxiliary, out);
    auxiliary.source = "brim_template";
    append_legacy_entity(print.brim(), auxiliary, out);
    return out;
}
} // namespace Slic3r::Predictive
