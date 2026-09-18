// Dependency-complete test: constructs actual preFlight entities, not mocks.
#include "libslic3r/Predictive/ArtifactJson.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include <iostream>
#include <stdexcept>

using namespace Slic3r;
using namespace Slic3r::Predictive;
int main()
{
    try {
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
        std::cout << "Native ExtrusionEntity traversal passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
