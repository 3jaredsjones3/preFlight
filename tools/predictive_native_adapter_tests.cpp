// Dependency-complete test: constructs actual preFlight entities, not mocks.
#include "libslic3r/Predictive/ArtifactJson.hpp"
#include "libslic3r/Predictive/ThermalSchedule.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include <iostream>
#include <stdexcept>

using namespace Slic3r;
using namespace Slic3r::Predictive;

// Real composite entities retain a width/flow discontinuity and a bridge role
// at a shared junction. This tests native capture, not generator coverage.
static void check_composite_segments()
{
    Polyline outward, inward;
    outward.points = {Point(0, 0), Point(10000000, 0)};
    inward.points = {Point(10000000, 0), Point(0, 0)};
    ExtrusionAttributes regular(ExtrusionRole::ExternalPerimeter, ExtrusionFlow(0.081, 0.45f, 0.2f));
    ExtrusionAttributes bridge(ExtrusionRole::OverhangPerimeter, ExtrusionFlow(0.125, 0.55f, 0.25f));
    ExtrusionPaths paths {ExtrusionPath(outward, regular), ExtrusionPath(inward, bridge)};
    ExtrusionLoop loop(paths);
    ExtrusionMultiPath multi(paths);
    LegacyContext context;
    context.object = 3;
    context.instance = 2;
    context.cancel_object = "native object 3 copy 2";
    context.print_z_mm = 0.4;
    context.source = "native_composite";
    LegacyAnalysis analysis;
    append_legacy_entity(loop, context, analysis);
    append_legacy_entity(multi, context, analysis);
    if (analysis.entities.size() != 6 || analysis.graph.paths.size() != 4)
        throw std::runtime_error("composite path boundaries lost");
    for (std::size_t i = 0; i < 4; ++i) {
        const auto &source = paths[i % 2];
        const auto &bead = analysis.graph.paths[i];
        const auto &entity = analysis.entities[bead.legacy_entity_id.value() - 1];
        if (bead.samples.size() != 2 || entity.context.object != context.object ||
            entity.context.instance != context.instance || entity.context.cancel_object != context.cancel_object ||
            entity.data.attributes.role_bits != legacy_role_bits(source.role()) ||
            bead.samples.front().width_mm != source.attributes().width ||
            bead.samples.back().height_mm != source.attributes().height ||
            std::abs(bead.samples.back().deposited_volume_mm3 - 10 * source.attributes().mm3_per_mm) > 1e-12)
            throw std::runtime_error("composite segment attributes or provenance lost");
    }
    if (loop.paths[0].polyline.points != outward.points || loop.paths[1].polyline.points != inward.points ||
        multi.paths[0].attributes().width != regular.width || multi.paths[1].attributes().mm3_per_mm != bridge.mm3_per_mm)
        throw std::runtime_error("composite source mutated");
}

int main()
{
    try {
        check_composite_segments();
        ExtrusionAttributes attributes(ExtrusionRole::SerpentineOverhang, ExtrusionFlow(0.081, 0.45f, 0.2f));
        attributes.flow_ratio = 1.5f;
        attributes.feature_id = 17;
        attributes.perimeter_index = 3;
        attributes.maybe_self_crossing = true;
        attributes.overhang_attributes = OverhangAttributes {0.1f, 0.2f, 0.3f};
        attributes.region_area_mm2 = 15;
        attributes.fill_pattern = 4;
        Polyline polyline;
        polyline.points = {Point(0, 0), Point(1000000, 0), Point(0, 0)};
        ExtrusionPath path(polyline, attributes);
        ExtrusionEntityCollection root;
        root.no_sort = true;
        root.entities.push_back(new ExtrusionLoop(path, elrContourInternalPerimeter));
        root.entities.push_back(new ExtrusionMultiPath(path));
        root.entities.push_back(new ExtrusionPathOriented(polyline, attributes));
        root.entities.push_back(new ExtrusionEntityCollection());
        LegacyContext context;
        context.print_z_mm = 0.2;
        context.source = "native_fixture";
        LegacyAnalysis first, second;
        append_legacy_entity(root, context, first);
        append_legacy_entity(root, context, second);
        if (artifact_json(first) != artifact_json(second) || first.entities.size() != 7 || first.graph.paths.size() != 3)
            throw std::runtime_error("native traversal changed source or topology");
        const auto &captured = first.entities[2].data;
        if (captured.attributes.feature_id != attributes.feature_id || captured.attributes.width_mm != attributes.width ||
            captured.attributes.mm3_per_mm != attributes.mm3_per_mm || captured.attributes.flow_ratio != attributes.flow_ratio ||
            captured.attributes.perimeter_index != attributes.perimeter_index || !captured.attributes.maybe_self_crossing ||
            !captured.attributes.overhang || (*captured.attributes.overhang)[2] != attributes.overhang_attributes->proximity_to_curled_lines ||
            captured.attributes.region_area_mm2 != attributes.region_area_mm2 || captured.attributes.fill_pattern != attributes.fill_pattern ||
            captured.attributes.role_bits != legacy_role_bits(attributes.role) || first.entities[5].data.can_reverse)
            throw std::runtime_error("native attributes lost");
        if (root.entities.size() != 4 || !root.no_sort || path.polyline.points != polyline.points)
            throw std::runtime_error("source mutated");
        MachineFingerprint machine;
        machine.content_hash = "sha256:native-test";
        ThermalModelContract thermal_model;
        thermal_model.model_id = "native-synthetic";
        thermal_model.synthetic = true;
        thermal_model.provenance = ThermalModelProvenance::Synthetic;
        const ThermalScheduleOptions thermal_options;
        const auto proposal = propose_thermal_schedule(first.graph, machine, thermal_model, thermal_options);
        if (!proposal.analysis_only || proposal.recommendation_eligible || proposal.baseline_order.size() != first.graph.paths.size() ||
            thermal_schedule_json(proposal) != thermal_schedule_json(propose_thermal_schedule(first.graph, machine, thermal_model, thermal_options)))
            throw std::runtime_error("production BeadGraphIR thermal analysis is not deterministic or is enabled");
        std::cout << "Native ExtrusionEntity traversal passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
