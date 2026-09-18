#include "ArtifactJson.hpp"

#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace Slic3r::Predictive {
namespace {
void value(std::ostream &out, const std::string &s)
{
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"';
}
void value(std::ostream &out, double n)
{
    if (!std::isfinite(n)) throw std::invalid_argument("Artifact JSON cannot encode a non-finite source value");
    out << (n == 0.0 ? 0.0 : n);
}
template<class T> void value(std::ostream &out, const T &v) { out << v; }
template<class T> void value(std::ostream &out, const std::optional<T> &v)
{
    if (v) value(out, *v); else out << "null";
}
template<class T, std::size_t N> void value(std::ostream &out, const std::array<T, N> &v)
{
    out << '[';
    for (std::size_t i = 0; i < N; ++i) { if (i) out << ','; value(out, v[i]); }
    out << ']';
}
// Explicit overload: optional arrays need the array overload visible at instantiation.
void value(std::ostream &out, const std::optional<std::array<double, 3>> &v)
{
    if (v) value(out, *v); else out << "null";
}
struct Object {
    std::ostream &out;
    bool first {true};
    explicit Object(std::ostream &stream) : out(stream) { out << '{'; }
    ~Object() { out << '}'; }
    template<class T> void field(const char *name, const T &v) {
        if (!first) out << ',';
        first = false;
        value(out, std::string(name)); out << ':'; value(out, v);
    }
};
void context_json(std::ostream &out, const LegacyContext &c)
{
    Object o(out);
    o.field("object", c.object); o.field("instance", c.instance); o.field("print_object", c.print_object);
    o.field("layer", c.layer); o.field("region", c.region); o.field("island", c.island);
    o.field("configured_tool", c.tool); o.field("material", c.material); o.field("cancel_object", c.cancel_object);
    o.field("generator", c.generator); o.field("speed_mm_s", c.speed_mm_s); o.field("source", c.source);
    o.field("print_z_mm", c.print_z_mm); o.field("shift_scaled", c.shift_scaled); o.field("coordinate_scale_mm", c.coordinate_scale_mm);
}
void attributes_json(std::ostream &out, const LegacyAttributes &a)
{
    Object o(out);
    o.field("role_bits", a.role_bits); o.field("width_mm", a.width_mm); o.field("height_mm", a.height_mm);
    o.field("mm3_per_mm", a.mm3_per_mm); o.field("flow_ratio", a.flow_ratio);
    o.field("maybe_self_crossing", a.maybe_self_crossing); o.field("overhang", a.overhang);
    o.field("perimeter_index", a.perimeter_index); o.field("feature_id", a.feature_id);
    o.field("region_area_mm2", a.region_area_mm2); o.field("fill_pattern", a.fill_pattern);
}
} // namespace

std::string artifact_json(const LegacyAnalysis &analysis)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\"schema_version\":\"m1-analysis-1\",\"mode\":\"analysis_only\",\"order\":\"source_storage\","
           "\"prediction\":{\"duration_s\":null,\"thermal\":null,\"bond\":null},\"unmodeled_downstream_passes\":[";
    for (std::size_t i = 0; i < analysis.unmodeled_downstream_passes.size(); ++i) {
        if (i) out << ',';
        value(out, analysis.unmodeled_downstream_passes[i]);
    }
    out << "],\"source_entities\":[";
    for (std::size_t i = 0; i < analysis.entities.size(); ++i) {
        if (i) out << ',';
        const auto &e = analysis.entities[i];
        out << "{\"id\":" << e.id << ",\"parent\":"; value(out, e.parent);
        out << ",\"child_index\":" << e.child_index << ",\"kind\":" << static_cast<int>(e.data.kind);
        out << ",\"can_reverse\":" << e.data.can_reverse << ",\"no_sort\":" << e.data.no_sort;
        out << ",\"loop_role\":"; value(out, e.data.loop_role);
        out << ",\"bead_path\":"; value(out, e.bead_path);
        out << ",\"context\":"; context_json(out, e.context);
        out << ",\"attributes\":"; attributes_json(out, e.data.attributes);
        out << ",\"points_scaled\":[";
        for (std::size_t j = 0; j < e.data.points_scaled.size(); ++j) {
            if (j) out << ',';
            value(out, e.data.points_scaled[j]);
        }
        out << "]}";
    }
    out << "],\"bead_graph\":[";
    for (std::size_t i = 0; i < analysis.graph.paths.size(); ++i) {
        if (i) out << ',';
        const auto &p = analysis.graph.paths[i];
        out << "{\"id\":" << p.id << ",\"source_entity\":"; value(out, p.legacy_entity_id);
        out << ",\"role\":" << static_cast<int>(p.role) << ",\"closed\":" << p.closed;
        out << ",\"extrusion_enabled\":" << p.extrusion_enabled << ",\"samples\":[";
        for (std::size_t j = 0; j < p.samples.size(); ++j) {
            if (j) out << ',';
            const auto &s = p.samples[j];
            // XYZ, width, height, incoming segment volume. Speed is nullable in source context.
            value(out, std::array<double, 6> {s.position.x, s.position.y, s.position.z, s.width_mm, s.height_mm, s.deposited_volume_mm3});
        }
        out << "]}";
    }
    out << "],\"certification\":{\"status\":\"analysis_only\",\"source_complete\":" << analysis.report.ok() << ",\"issues\":[";
    for (std::size_t i = 0; i < analysis.report.issues.size(); ++i) {
        if (i) out << ',';
        const auto &issue = analysis.report.issues[i];
        Object o(out);
        o.field("severity", std::string(issue.severity == Severity::Error ? "error" : issue.severity == Severity::Warning ? "warning" : "information"));
        o.field("code", issue.code); o.field("message", issue.message); o.field("path_id", issue.path_id);
    }
    out << "]}}\n";
    return out.str();
}
void write_artifact_json(std::ostream &output, const LegacyAnalysis &analysis)
{
    output << artifact_json(analysis);
    if (!output) throw std::runtime_error("Failed to write analysis artifact");
}
} // namespace Slic3r::Predictive
