#include "libslic3r/Predictive/ArtifactJson.hpp"
#include "libslic3r/Predictive/CanonicalGCodeComparator.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <stdexcept>

using namespace Slic3r;
using namespace Slic3r::Predictive;
namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
LegacyEntitySnapshot path(ExtrusionRole role, double width = 0.45)
{
    LegacyEntitySnapshot s;
    s.kind = LegacyEntityKind::Path;
    s.can_reverse = true;
    s.attributes.role_bits = legacy_role_bits(role);
    s.attributes.width_mm = width;
    s.attributes.height_mm = 0.2;
    s.attributes.mm3_per_mm = 0.08141592653589794;
    s.attributes.perimeter_index = std::uint16_t {0};
    s.attributes.feature_id = std::uint16_t {7};
    s.attributes.region_area_mm2 = 36;
    s.points_scaled = {{-1000000, 0}, {2000000, 0}, {2000000, 4000000}};
    return s;
}
LegacyContext context()
{
    LegacyContext c;
    c.object = 0; c.instance = 0; c.print_object = 0; c.layer = 2; c.region = 0; c.island = 1;
    c.tool = 0; c.material = "PLA"; c.print_z_mm = 0.6;
    c.source = "fixture"; c.shift_scaled = {10000000, 20000000};
    c.cancel_object = "fixture-object-0";
    return c;
}
LegacyAnalysis fixture(const std::string &name)
{
    auto c = context();
    LegacyEntitySnapshot root;
    root.kind = LegacyEntityKind::Collection; root.no_sort = true;
    if (name == "mechanical") {
        c.generator = "classic";
        LegacyEntitySnapshot loop;
        loop.kind = LegacyEntityKind::Loop; loop.loop_role = 1;
        auto p = path(ExtrusionRole::ExternalPerimeter);
        p.points_scaled.push_back(p.points_scaled.front());
        loop.children = {p};
        root.children = {loop, path(ExtrusionRole::InternalInfill), path(ExtrusionRole::TopSolidInfill), path(ExtrusionRole::Ironing)};
    } else if (name == "thin_wall") {
        c.generator = "arachne";
        LegacyEntitySnapshot multi;
        multi.kind = LegacyEntityKind::MultiPath; multi.can_reverse = true;
        auto a = path(ExtrusionRole::Perimeter, 0.31);
        auto b = path(ExtrusionRole::GapFill, 0.67);
        b.points_scaled = {a.points_scaled.back(), {5000000, 4000000}};
        b.attributes.mm3_per_mm = 0.127;
        multi.children = {a, b};
        root.children = {multi};
    } else if (name == "bridge_support") {
        root.children = {path(ExtrusionRole::BridgeInfill), path(ExtrusionRole::OverhangPerimeter),
            path(ExtrusionRole::SupportMaterial), path(ExtrusionRole::SupportMaterialInterface)};
        root.children[1].attributes.overhang = {0.1, 0.7, 0.4};
    } else if (name == "serpentine") {
        c.generator = "athena";
        root.children = {path(ExtrusionRole::Serpentine), path(ExtrusionRole::SerpentineOverhang)};
        root.children[0].kind = LegacyEntityKind::OrientedPath;
        root.children[0].can_reverse = false;
        root.children[0].attributes.maybe_self_crossing = true;
    } else if (name == "interlocking") {
        c.generator = "athena";
        root.children = {path(ExtrusionRole::InterlockingPerimeter), path(ExtrusionRole::GapFill)};
        root.children[0].attributes.flow_ratio = 1.5;
        root.children[0].attributes.mm3_per_mm = 0.12;
    } else if (name == "multi_material") {
        root.children = {path(ExtrusionRole::Perimeter), path(ExtrusionRole::SupportMaterialInterface), path(ExtrusionRole::WipeTower)};
    } else if (name == "multi_object") {
        root.children = {path(ExtrusionRole::Skirt), path(ExtrusionRole::Perimeter)};
    } else throw std::runtime_error("Unknown fixture");
    LegacyAnalysis result;
    const LegacyContextResolver resolver = [name](const LegacyAttributes &a, const LegacyContext &input) {
        auto output = input;
        if (name == "multi_material" && legacy_path_role(a.role_bits) == PathRole::SupportInterface) {
            output.tool = 1; output.material = "PVA";
        }
        return output;
    };
    append_legacy_snapshot(root, c, result, resolver);
    if (name == "multi_object") {
        c.object = 1; c.instance = 2; c.print_object = 1;
        c.shift_scaled = {-20000000, 0}; c.cancel_object = "fixture-object-1-copy-2";
        append_legacy_snapshot(root, c, result, resolver);
    }
    return result;
}
void invariants()
{
    auto root = path(ExtrusionRole::SerpentineOverhang);
    root.attributes.flow_ratio = 1.7;
    LegacyAnalysis a;
    append_legacy_snapshot(root, context(), a);
    require(a.graph.paths.size() == 1 && a.entities.size() == 1, "path cardinality");
    const auto &p = a.graph.paths[0];
    require(p.samples[0].position.x == 9.0 && p.samples[0].position.y == 20.0, "scaled shift mapping");
    require(p.samples[0].deposited_volume_mm3 == 0.0, "first sample must not deposit");
    require(std::abs(p.samples[1].deposited_volume_mm3 - 3 * root.attributes.mm3_per_mm) < 1e-14, "flow must not be rectangular or multiplied twice");
    require(a.entities[0].data.attributes.role_bits == legacy_role_bits(ExtrusionRole::SerpentineOverhang), "role modifiers lost");
    require(!a.entities[0].context.speed_mm_s && p.samples[0].speed_mm_s == 0.0, "unknown speed fabricated");
    require(p.dependencies.empty(), "source order must not invent physical dependencies");
    require(!validate_bead_graph(a.graph).ok(), "analysis inventory permitted in predictive emission pipeline");
    auto thin = fixture("thin_wall");
    require(thin.entities[2].parent == thin.entities[3].parent, "multipath grouping lost");
    require(thin.graph.paths[0].samples.back().width_mm != thin.graph.paths[1].samples.front().width_mm, "junction widths merged");
    LegacyEntitySnapshot empty;
    empty.kind = LegacyEntityKind::Collection;
    append_legacy_snapshot(empty, context(), a);
    require(a.entities.size() == 2 && a.graph.paths.size() == 1, "empty collection discarded");
    root.points_scaled.clear();
    append_legacy_snapshot(root, context(), a);
    require(!a.report.ok() && a.graph.paths.back().samples.empty(), "degenerate input silently repaired");
    append_legacy_snapshot({}, context(), a);
    require(a.report.issues.back().code == "adapter.unsupported_entity", "unknown entity silently discarded");
    root = path(ExtrusionRole::Perimeter); root.attributes.mm3_per_mm = 0;
    LegacyAnalysis zero;
    append_legacy_snapshot(root, context(), zero);
    require(!zero.graph.paths[0].extrusion_enabled && zero.graph.paths[0].samples[1].deposited_volume_mm3 == 0, "zero flow fabricated");
    auto encoded = fixture("mechanical");
    encoded.entities[0].context.material = "quote\"slash\\\n\t\x01";
    require(artifact_json(encoded).find("quote\\\"slash\\\\\\u000a\\u0009\\u0001") != std::string::npos, "JSON string escaping");
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    const auto expected = artifact_json(encoded);
    const auto old = std::locale::global(std::locale(std::locale::classic(), new Comma));
    require(artifact_json(encoded) == expected, "JSON depends on global locale");
    std::locale::global(old);
    encoded.entities[0].context.print_z_mm = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try { (void)artifact_json(encoded); } catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "non-finite JSON accepted");
    for (unsigned int bit = 0; bit < 16; ++bit)
        require(legacy_role_bits(ExtrusionRole(static_cast<ExtrusionRoleModifier>(bit))) == (1u << bit), "role bit lost");
}
void comparator_tests()
{
    require(compare_gcode("G1 X1\n", "G1 X1\n").byte_identical, "byte identity");
    require(compare_gcode("G01X1.00Y-0.0E.5\r\nM83\n", "G1 E0.500 Y0 X+1\nM83\n").equivalent, "numeric canonicalization");
    const std::string base = "G90\nM83\nT0\nG1 X1 Y2 E0.5 F1200\n; printing object cube id:0 copy 0\n";
    for (const auto &pair : std::vector<std::pair<std::string, std::string>> {
        {"G90", "G91"}, {"M83", "M82"}, {"T0", "T1"}, {"X1", "X2"}, {"E0.5", "E0.6"},
        {"F1200", "F1201"}, {"copy 0", "copy 1"}}) {
        auto changed = base;
        changed.replace(changed.find(pair.first), pair.first.size(), pair.second);
        require(!compare_gcode(base, changed).equivalent, "protected G-code mutation accepted");
    }
    require(!compare_gcode("SET_PRESSURE_ADVANCE ADVANCE=0.02\n", "SET_PRESSURE_ADVANCE ADVANCE=0.03\n").equivalent, "unknown macro ignored");
    require(!compare_gcode("G1 X1 X2\n", "G1 X2\n").equivalent, "duplicate words discarded");
    require(!compare_gcode("G1 X1 G91\n", "G1 G91 X1\n").equivalent, "multi-command block reordered");
    require(!compare_gcode(";TYPE:Perimeter\n", ";TYPE:Bridge\n").equivalent, "semantic comments discarded");
    require(!compare_gcode("M117 hello world\n", "M117 hello  world\n").equivalent, "opaque text normalized");
    require(!compare_gcode("G1 X1\n", "G1 X1\nM104 S0\n").equivalent, "trailing command ignored");
    require(compare_gcode("; generated on old\nG1 X1\n", "; generated on new\nG1 X1\n", {"; generated on "}).equivalent, "explicit volatile metadata");
}
} // namespace
int main(int argc, char **argv)
{
    try {
        require(argc >= 2, "usage: predictive_adapter_tests GOLDEN_DIR [--update]");
        invariants(); comparator_tests();
        const bool update = argc == 3 && std::string(argv[2]) == "--update";
        for (const char *name : {"mechanical", "thin_wall", "bridge_support", "multi_object", "serpentine", "interlocking", "multi_material"}) {
            const auto actual = artifact_json(fixture(name));
            require(actual == artifact_json(fixture(name)), "report not deterministic");
            const auto file = std::filesystem::path(argv[1]) / (std::string(name) + ".artifact.json");
            if (update) { std::ofstream out(file, std::ios::binary); out << actual; require(bool(out), "golden write failed"); }
            else {
                std::ifstream in(file, std::ios::binary);
                require(bool(in), "golden missing; do not auto-approve new output");
                const std::string expected(std::istreambuf_iterator<char>(in), {});
                if (actual != expected) throw std::runtime_error(std::string("golden mismatch: ") + name);
            }
        }
        std::cout << "Adapter invariants, comparator mutations and seven entity goldens passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
