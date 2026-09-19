#include "Verifier.hpp"
#include "Schemas.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace JSlice::Verification {
namespace {
constexpr double epsilon = 1e-8; // arithmetic boundary slack in mm, not command equivalence
using V3 = std::array<double, 3>;
using V4 = std::array<double, 4>;
struct Failure : std::runtime_error {
    std::string code;
    Failure(std::string c, const std::string &message) : std::runtime_error(message), code(std::move(c)) {}
};
void demand(bool ok, const std::string &code, const std::string &message)
{
    if (!ok) throw Failure(code, message);
}
std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
struct Command {
    std::string code;
    std::map<char, std::optional<double>> words;
    bool has(char c) const { return words.contains(c); }
    double number(char c) const {
        demand(has(c) && words.at(c).has_value(), "syntax.parameter", std::string("missing numeric ") + c);
        return *words.at(c);
    }
    double value(char c, double fallback) const { return has(c) ? number(c) : fallback; }
    void allow(std::string_view allowed, bool mode = false) const {
        for (const auto &[key, value] : words) {
            (void)value;
            demand(allowed.find(key) != std::string_view::npos, mode ? "modal.parameters" : "syntax.parameter",
                std::string("unexpected parameter ") + key + " for " + code);
        }
    }
};
bool digit(char c) { return c >= '0' && c <= '9'; }
Command parse(std::string_view text)
{
    Command out;
    demand(!text.empty() && (text.front() == 'G' || text.front() == 'M' || text.front() == 'T'),
        "command.unsupported", "expected explicit uppercase G, M or T command; macros/line numbers unsupported");
    const char family = text.front();
    text.remove_prefix(1);
    std::size_t count = 0;
    while (count < text.size() && digit(text[count])) ++count;
    demand(count > 0 && count < 7, "syntax.command", "invalid command number");
    int id = 0;
    std::from_chars(text.data(), text.data() + count, id);
    out.code = family + std::to_string(id);
    text.remove_prefix(count);
    demand(text.empty() || text.front() != '.', "command.unsupported", "command subcodes unsupported");
    while (!(text = trim(text)).empty()) {
        const char key = text.front();
        demand(key >= 'A' && key <= 'Z', "syntax.word", "invalid parameter or trailing data");
        demand(!out.has(key), "syntax.duplicate", "duplicate parameter");
        text.remove_prefix(1);
        text = trim(text);
        std::string lower(text.substr(0, std::min<std::size_t>(text.size(), 9)));
        for (char &c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (!lower.empty() && (lower.front() == '+' || lower.front() == '-')) lower.erase(0, 1);
        demand(!lower.starts_with("nan") && !lower.starts_with("inf"), "numeric.nonfinite", "non-finite numeric word");
        std::size_t n = 0;
        if (n < text.size() && (text[n] == '-' || text[n] == '+')) ++n;
        bool have_digit = false;
        while (n < text.size() && digit(text[n])) { have_digit = true; ++n; }
        if (n < text.size() && text[n] == '.') {
            ++n;
            while (n < text.size() && digit(text[n])) { have_digit = true; ++n; }
        }
        if (n == 0) { out.words[key] = std::nullopt; continue; }
        demand(have_digit, "syntax.number", "malformed number");
        auto number = text.substr(0, n);
        if (number.front() == '+') number.remove_prefix(1);
        double value = 0;
        const auto conversion = std::from_chars(number.data(), number.data() + number.size(), value, std::chars_format::fixed);
        demand(conversion.ec != std::errc::result_out_of_range && std::isfinite(value), "numeric.nonfinite", "number exceeds finite range");
        demand(conversion.ec == std::errc() && conversion.ptr == number.data() + number.size(), "syntax.number", "malformed decimal number");
        demand(std::abs(value) <= 1e7, "numeric.sanity", "numeric word exceeds supported magnitude");
        out.words[key] = value;
        text.remove_prefix(n);
    }
    return out;
}
std::size_t index(double n, std::size_t limit, const std::string &code)
{
    demand(n >= 0 && std::floor(n) == n && n < static_cast<double>(limit), code, "index outside configured range");
    return static_cast<std::size_t>(n);
}

struct Replay {
    const Json &machine;
    const Json &manifest;
    Json findings = Json::array();
    std::size_t line = 0, offset = 0, commands = 0, motions = 0;
    V3 position {}, origin {};
    V4 velocity {}, acceleration {};
    double e = 0, unit = 0, feed = 0, speed_factor = 1, saved_speed = 1;
    bool saved_speed_valid = false, executed = false, absolute = true, e_absolute = true;
    bool coordinate_known = false, extrusion_known = false;
    double print_accel = 0, travel_accel = 0, retract_accel = 0, bed_target = 0, bed_lower = 0;
    std::size_t tool = 0;
    std::vector<double> target, lower, debt, flow_factor;
    std::vector<int> fans;
    std::set<std::string> expected, declared, used;
    std::string declaration, active;
    bool objects_info_seen = false;
    std::size_t end_markers = 0;
    double peak_flow = 0, peak_feed = 0;
    std::vector<std::string> command_bytes, command_objects;
    std::vector<std::size_t> command_tools;
    std::vector<std::size_t> command_lines;
    struct Deposition { V3 start {}, end {}; std::size_t line = 0, command = 0; std::string object; };
    std::vector<Deposition> depositions;
    Replay(const Json &fp, const Json &mf) : machine(fp), manifest(mf) {
        const auto &initial = mf.at("initial_state");
        position = initial.at("position_mm").get<V3>();
        e = initial.at("e_mm").get<double>();
        tool = initial.at("tool").get<std::size_t>();
        lower = initial.at("heater_temperature_c").get<std::vector<double>>();
        target.resize(lower.size(), 0);
        bed_lower = initial.at("bed_temperature_c").get<double>();
        fans = initial.at("fan_pwm").get<std::vector<int>>();
        debt.resize(fp.at("tools").size(), 0);
        flow_factor.resize(debt.size(), 1);
        velocity = fp.at("axis_velocity_mm_s").get<V4>();
        acceleration = fp.at("axis_acceleration_mm_s2").get<V4>();
        print_accel = travel_accel = retract_accel = fp.at("max_acceleration_mm_s2").get<double>();
        std::set<std::pair<std::uint64_t, std::uint64_t>> identities;
        for (const auto &name : mf.at("objects")) {
            const auto label = name.get<std::string>();
            demand(expected.insert(label).second, "manifest.objects", "duplicate declared object identity");
            std::smatch parts;
            demand(std::regex_match(label, parts, std::regex(".+ id:([0-9]+) copy ([0-9]+)")), "manifest.objects", "invalid object label grammar");
            std::array<std::uint64_t, 2> identity {};
            for (std::size_t i = 0; i < 2; ++i) {
                const auto text = parts[i + 1].str();
                const auto result = std::from_chars(text.data(), text.data() + text.size(), identity[i]);
                demand(result.ec == std::errc() && result.ptr == text.data() + text.size(), "manifest.objects", "object index overflow");
            }
            demand(identities.emplace(identity[0], identity[1]).second, "manifest.objects", "object/instance ID reused under another name");
        }
        demand(tool < debt.size() && lower.size() == fp.at("heaters").size() && fans.size() == fp.at("fan_count").get<std::size_t>(),
            "manifest.initial_state", "initial tool/heater/fan dimensions mismatch");
        for (std::size_t i = 0; i < lower.size(); ++i)
            demand(lower[i] <= fp.at("heaters")[i].get<double>(), "manifest.initial_state", "initial heater exceeds bound");
        demand(bed_lower <= fp.at("max_bed_temperature_c").get<double>(), "manifest.initial_state", "initial bed exceeds bound");
        for (std::size_t i = 0; i < 3; ++i)
            demand(fp.at("build_min_mm")[i].get<double>() < fp.at("build_max_mm")[i].get<double>(), "schema.machine", "empty build volume");
        for (const auto &t : fp.at("tools")) {
            const auto heater = t.at("heater").get<std::size_t>();
            demand(heater < lower.size(), "schema.machine", "invalid heater mapping");
            demand(t.at("min_extrusion_temperature_c").get<double>() <= fp.at("heaters")[heater].get<double>(), "schema.machine", "inconsistent extrusion temperature bounds");
            // Marlin tool-offset compensation and implicit toolchange moves
            // require another dialect contract; v1 qualifies shared-nozzle tools.
            demand(t.at("offset_mm").get<V3>() == V3{}, "tool.offset_unsupported", "nonzero tool offsets are not qualified by this dialect");
        }
    }
    void add(const std::string &code, const std::string &message, const std::string &severity = "error") {
        findings.push_back({{"line", line}, {"byte_offset", offset}, {"severity", severity}, {"code", code}, {"message", message}});
    }
    void bounds(const V3 &p) const {
        const auto &t = machine.at("tools")[tool];
        for (std::size_t i = 0; i < 3; ++i) {
            const double nozzle = p[i] + t.at("offset_mm")[i].get<double>();
            const double lo = machine.at("build_min_mm")[i].get<double>(), hi = machine.at("build_max_mm")[i].get<double>();
            demand(std::isfinite(nozzle) && std::abs(nozzle) <= 1e7, "numeric.sanity", "invalid physical coordinate");
            demand(nozzle >= lo - epsilon && nozzle <= hi + epsilon, "motion.bounds", "nozzle outside build volume");
            demand(nozzle + t.at("envelope_min_mm")[i].get<double>() >= lo - epsilon &&
                nozzle + t.at("envelope_max_mm")[i].get<double>() <= hi + epsilon,
                "tool.envelope", "tool bounding box outside build volume");
        }
    }
    void comment(std::string_view s) {
        s = trim(s);
        if (s == "preflight_config = end") ++end_markers;
        const auto start = std::string_view("printing object "), end = std::string_view("stop printing object ");
        if (s.starts_with("printing object") || s.starts_with("stop printing object")) {
            const bool begin = s.starts_with(start);
            demand(begin || s.starts_with(end), "object.label", "malformed object boundary");
            const std::string name(s.substr(begin ? start.size() : end.size()));
            demand(std::regex_match(name, std::regex(".+ id:[0-9]+ copy [0-9]+")) && expected.contains(name), "object.label", "unknown or malformed object identity");
            auto &current = executed ? active : declaration;
            if (begin) {
                demand(current.empty(), "object.boundary", "nested or unclosed object boundary");
                if (executed) {
                    demand(declared.contains(name), "object.boundary", "object started without header declaration");
                    used.insert(name);
                } else demand(!declared.contains(name), "object.boundary", "duplicate header declaration");
                current = name;
            } else {
                demand(current == name, "object.boundary", "mismatched object end");
                if (!executed) declared.insert(name);
                current.clear();
            }
        } else if (s.starts_with("objects_info")) {
            constexpr std::string_view prefix = "objects_info = ";
            demand(s.starts_with(prefix) && !objects_info_seen, "object.metadata", "malformed or duplicate object metadata");
            const auto info = strict_json(s.substr(prefix.size()));
            demand(info.is_object() && info.contains("objects") && info.at("objects").is_array(), "object.metadata", "invalid object metadata");
            std::set<std::string> names;
            for (const auto &object : info.at("objects")) {
                demand(object.is_object() && object.contains("name") && object.at("name").is_string(), "object.metadata", "invalid object metadata name");
                demand(names.insert(object.at("name").get<std::string>()).second, "object.metadata", "duplicate object metadata name");
            }
            demand(names == expected, "object.metadata", "object metadata disagrees with manifest");
            objects_info_seen = true;
        } else if (s.starts_with("JS_FINGERPRINT_SHA256")) {
            constexpr std::string_view prefix = "JS_FINGERPRINT_SHA256=";
            demand(s.starts_with(prefix) && s.substr(prefix.size()) == manifest.at("fingerprint_sha256").get<std::string>(), "binding.fingerprint", "G-code fingerprint metadata mismatch");
        }
    }
    void motion(const Command &c) {
        c.allow("XYZEF");
        demand(unit > 0 && coordinate_known && extrusion_known, "modal.unknown", "motion before units/coordinate/extrusion modes are established");
        double next_feed = feed;
        if (c.has('F')) {
            next_feed = c.number('F') * unit / 60;
            demand(next_feed > 0, "motion.feedrate", "feedrate must be positive");
        }
        const double speed = next_feed * speed_factor;
        demand(speed <= machine.at("max_feedrate_mm_s").get<double>() + epsilon, "motion.feedrate", "requested feedrate exceeds machine limit");
        V3 next = position;
        for (std::size_t i = 0; i < 3; ++i) if (c.has("XYZ"[i]))
            next[i] = c.number("XYZ"[i]) * unit + (absolute ? origin[i] : position[i]);
        double next_e = e;
        if (c.has('E')) next_e = c.number('E') * unit + (e_absolute ? 0 : e);
        const double delta_e = (next_e - e) * flow_factor[tool];
        demand(std::isfinite(next_e) && std::abs(next_e) <= 1e7 && std::abs(delta_e) <= machine.at("max_extrusion_step_mm").get<double>() + epsilon,
            "extrusion.continuity", "extrusion coordinate or single move discontinuity");
        bounds(position);
        bounds(next); // straight segment + translated AABB in a convex box
        double length = 0;
        for (std::size_t i = 0; i < 3; ++i) length += (next[i] - position[i]) * (next[i] - position[i]);
        length = std::sqrt(length);
        // Feed on an E-only move is in commanded filament mm/min. M221 changes
        // physical drive distance, not that commanded duration.
        const double distance = length > 0 ? length : std::abs(next_e - e);
        if (distance > 0) {
            demand(speed > 0, "modal.feedrate", "move has no known positive feedrate");
            // Some firmware excludes E-only moves from speed override. Use the
            // faster of scaled/unscaled feed to avoid an optimistic bound.
            const double duration = distance / (length > 0 ? speed : std::max(speed, next_feed));
            for (std::size_t i = 0; i < 4; ++i) {
                const double travel = i < 3 ? std::abs(next[i] - position[i]) : std::abs(delta_e);
                demand(travel / duration <= velocity[i] + epsilon, "motion.axis_velocity", std::string("axis velocity exceeded: ") + "XYZE"[i]);
            }
            const auto &t = machine.at("tools")[tool];
            if (delta_e > 0) {
                const auto heater = t.at("heater").get<std::size_t>();
                demand(lower[heater] >= t.at("min_extrusion_temperature_c").get<double>(), "extrusion.cold", "positive E motion without established minimum temperature");
                const double diameter = t.at("filament_diameter_mm").get<double>();
                const double flow = delta_e * std::numbers::pi * diameter * diameter / (4 * duration);
                demand(flow <= t.at("max_flow_mm3_s").get<double>() + epsilon, "extrusion.flow", "volumetric drive flow exceeded (including unretraction)");
                peak_flow = std::max(peak_flow, flow);
                if (length > 0) depositions.push_back({position, next, line, commands, active});
            }
        }
        const double next_debt = std::max(0.0, debt[tool] - delta_e);
        demand(next_debt <= machine.at("max_retraction_mm").get<double>() + epsilon, "extrusion.retraction", "cumulative unrecovered retraction exceeds limit");
        // G92 must not erase this physical retraction ledger.
        debt[tool] = next_debt;
        position = next;
        e = next_e;
        feed = next_feed;
        peak_feed = std::max(peak_feed, speed);
        ++motions;
    }
    void command(const Command &c) {
        if (c.code == "G0" || c.code == "G1") { motion(c); return; }
        if (c.code == "G2" || c.code == "G3") throw Failure("arc.unsupported", "arcs require independent sweep/velocity analysis and are rejected");
        if (c.code == "G20" || c.code == "G21") { c.allow("", true); unit = c.code == "G20" ? 25.4 : 1; return; }
        if (c.code == "G90" || c.code == "G91") {
            c.allow("", true); absolute = e_absolute = c.code == "G90";
            coordinate_known = extrusion_known = true; return;
        }
        if (c.code == "M82" || c.code == "M83") { c.allow("", true); e_absolute = c.code == "M82"; extrusion_known = true; return; }
        if (c.code == "G92") {
            c.allow("XYZE");
            demand(unit > 0 && !c.words.empty(), "modal.reset", "G92 requires known units and explicit axes");
            for (std::size_t i = 0; i < 3; ++i) if (c.has("XYZ"[i])) origin[i] = position[i] - c.number("XYZ"[i]) * unit;
            if (c.has('E')) e = c.number('E') * unit;
            return;
        }
        if (c.code.front() == 'T') {
            c.allow("");
            const auto next_tool = index(std::stod(c.code.substr(1)), debt.size(), "tool.invalid");
            const auto previous = tool; tool = next_tool;
            try { bounds(position); } catch (...) { tool = previous; throw; }
            return; // fingerprint assumes no implicit motion, E reset, load or heater change
        }
        if (c.code == "M104" || c.code == "M109" || c.code == "M140" || c.code == "M190") {
            const bool bed = c.code == "M140" || c.code == "M190";
            const bool wait = c.code == "M109" || c.code == "M190";
            c.allow(bed ? (wait ? "SR" : "S") : (wait ? "SRT" : "ST"));
            demand(c.has('S') != c.has('R'), "temperature.syntax", "exactly one temperature target required");
            const double temperature = c.has('S') ? c.number('S') : c.number('R');
            const auto selected = bed ? tool : index(c.value('T', static_cast<double>(tool)), debt.size(), "tool.invalid");
            const auto heater = machine.at("tools")[selected].at("heater").get<std::size_t>();
            const double maximum = bed ? machine.at("max_bed_temperature_c").get<double>() : machine.at("heaters")[heater].get<double>();
            demand(temperature >= 0 && temperature <= maximum, "temperature.bounds", "temperature target outside configured range");
            auto &target_ref = bed ? bed_target : target[heater];
            auto &lower_ref = bed ? bed_lower : lower[heater];
            target_ref = temperature;
            lower_ref = wait ? temperature : std::min(lower_ref, temperature);
            return;
        }
        if (c.code == "M106" || c.code == "M107") {
            c.allow(c.code == "M106" ? "SP" : "P");
            const auto fan = index(c.value('P', 0), fans.size(), "fan.index");
            const double speed = c.code == "M107" ? 0 : c.value('S', 255);
            demand(speed >= 0 && speed <= 255 && std::floor(speed) == speed, "fan.range", "fan PWM outside byte range");
            fans[fan] = static_cast<int>(speed); return;
        }
        if (c.code == "M220") {
            c.allow("SBR");
            demand(!c.words.empty(), "syntax.parameter", "empty feedrate override");
            demand(!(c.has('B') && c.has('R')), "modal.speed_backup", "ambiguous backup/restore");
            for (char flag : {'B', 'R'}) if (c.has(flag)) demand(!c.words.at(flag).has_value(), "syntax.parameter", "backup/restore flags must be bare");
            if (c.has('S')) demand(c.number('S') > 0 && c.number('S') <= 1000, "motion.feedrate", "feedrate override outside supported range");
            if (c.has('R')) demand(saved_speed_valid, "modal.speed_backup", "restore without backup");
            if (c.has('B')) { saved_speed = speed_factor; saved_speed_valid = true; }
            if (c.has('R')) speed_factor = saved_speed;
            if (c.has('S')) speed_factor = c.number('S') / 100;
            return;
        }
        if (c.code == "M221") {
            c.allow("ST");
            const auto selected = index(c.value('T', static_cast<double>(tool)), debt.size(), "tool.invalid");
            const double factor = c.number('S');
            demand(factor > 0 && factor <= 1000, "extrusion.flow", "flow override outside supported range");
            flow_factor[selected] = factor / 100; return;
        }
        if (c.code == "M201" || c.code == "M203") {
            c.allow("XYZE"); demand(unit > 0 && !c.words.empty(), "modal.unknown", "axis limits require units and axes");
            auto &current = c.code == "M201" ? acceleration : velocity;
            const auto &maximum = machine.at(c.code == "M201" ? "axis_acceleration_mm_s2" : "axis_velocity_mm_s");
            V4 next = current;
            for (std::size_t i = 0; i < 4; ++i) if (c.has("XYZE"[i])) {
                next[i] = c.number("XYZE"[i]) * unit;
                demand(next[i] > 0 && next[i] <= maximum[i].get<double>(), "motion.config_limit", "axis configuration exceeds fingerprint");
            }
            current = next; return;
        }
        if (c.code == "M204") {
            c.allow("SPRT"); demand(unit > 0 && !c.words.empty(), "modal.unknown", "acceleration requires known units");
            demand(!(c.has('S') && (c.has('P') || c.has('T'))), "syntax.parameter", "ambiguous acceleration selection");
            for (const auto &[key, ignored] : c.words) {
                (void)ignored;
                demand(c.number(key) > 0 && c.number(key) * unit <= machine.at("max_acceleration_mm_s2").get<double>(), "motion.config_limit", "acceleration exceeds fingerprint");
            }
            if (c.has('S')) print_accel = travel_accel = c.number('S') * unit;
            if (c.has('P')) print_accel = c.number('P') * unit;
            if (c.has('T')) travel_accel = c.number('T') * unit;
            if (c.has('R')) retract_accel = c.number('R') * unit;
            return;
        }
        if (c.code == "M900") {
            c.allow("K"); demand(c.number('K') == 0, "command.unsupported", "nonzero pressure advance has unmodeled E dynamics"); return;
        }
        if (c.code == "G4" || c.code == "M1") {
            c.allow("PS"); demand(c.has('P') != c.has('S'), "syntax.parameter", "exactly one dwell duration required");
            const double seconds = c.has('S') ? c.number('S') : c.number('P') / 1000;
            demand(seconds >= 0 && seconds <= 86400 && (c.code != "M1" || seconds > 0), "command.unsupported", "unbounded or invalid pause"); return;
        }
        if (c.code == "M300") {
            c.allow("SP"); demand(c.number('S') >= 0 && c.number('S') <= 20000 && c.number('P') >= 0 && c.number('P') <= 60000,
                "syntax.parameter", "tone outside supported range"); return;
        }
        if (c.code == "M400") { c.allow(""); return; }
        throw Failure("command.unsupported", "unmodeled safety-relevant command " + c.code);
    }
    Json state() const {
        return {{"position_mm", position}, {"coordinate_origin_mm", origin}, {"e_mm", e}, {"tool", tool},
            {"units_mm", unit}, {"coordinate_mode", coordinate_known ? Json(absolute ? "absolute" : "relative") : Json(nullptr)},
            {"extrusion_mode", extrusion_known ? Json(e_absolute ? "absolute" : "relative") : Json(nullptr)},
            {"feedrate_mm_s", feed}, {"speed_factor", speed_factor}, {"saved_speed_factor", saved_speed_valid ? Json(saved_speed) : Json(nullptr)},
            {"axis_velocity_mm_s", velocity}, {"axis_acceleration_mm_s2", acceleration},
            {"print_acceleration_mm_s2", print_accel}, {"travel_acceleration_mm_s2", travel_accel}, {"retract_acceleration_mm_s2", retract_accel},
            {"flow_factors", flow_factor}, {"heater_targets_c", target}, {"heater_lower_bounds_c", lower},
            {"bed_target_c", bed_target}, {"bed_lower_bound_c", bed_lower}, {"fan_pwm", fans},
            {"retraction_debt_mm", debt}, {"active_object", active.empty() ? Json(nullptr) : Json(active)}};
    }
};
}

Json verify(std::string_view program, std::string_view fingerprint, std::string_view manifest, std::string_view work_packet)
{
    Json report {{"schema_version", "js-verifier-report-1"}, {"accepted", false}, {"scope", "conditional_static_checks"},
        {"findings", Json::array()}, {"lines_scanned", 0}, {"commands_seen", 0}, {"replay_complete", false},
        {"input_sha256", {{"program", sha256(program)}, {"fingerprint", sha256(fingerprint)}, {"manifest", sha256(manifest)},
            {"work_packet", sha256(work_packet)}, {"work_packet_canonical", sha256(work_packet)}}}};
    const auto input_error = [&](const std::string &code, const std::string &message) {
        report["findings"].push_back({{"line", 0}, {"byte_offset", 0}, {"severity", "error"}, {"code", code}, {"message", message}});
    };
    Json fp, mf, packet;
    try { fp = strict_json(fingerprint); validate_schema(fp, strict_json(machine_schema)); }
    catch (const std::exception &error) { input_error("schema.machine", error.what()); return report; }
    try { mf = strict_json(manifest); validate_schema(mf, strict_json(manifest_schema)); }
    catch (const std::exception &error) { input_error("schema.manifest", error.what()); return report; }
    try { packet = strict_json(work_packet); report["input_sha256"]["work_packet_canonical"] = sha256(canonical_json(packet)); validate_schema(packet, strict_json(work_packet_schema)); }
    catch (const std::exception &error) { input_error("schema.work_packet", error.what()); return report; }
    try {
        demand(packet.at("program_sha256") == sha256(program), "binding.program", "work packet program hash mismatch");
        demand(packet.at("fingerprint_sha256") == sha256(canonical_json(fp)), "binding.fingerprint", "work packet canonical fingerprint hash mismatch");
        demand(packet.at("manifest_sha256") == sha256(canonical_json(mf)), "binding.manifest", "work packet canonical manifest hash mismatch");
        demand(packet.at("compiler").at("commit") == mf.at("compiler_commit"), "binding.compiler", "work packet compiler commit differs from manifest");
        demand(packet.at("compiler").at("build_id") == mf.at("compiler_build_id"), "binding.compiler", "work packet compiler build identity differs from manifest");
        demand(packet.at("schema_versions").at("machine") == "js-machine-1" &&
            packet.at("schema_versions").at("manifest") == "js-verification-1" &&
            packet.at("schema_versions").at("packet") == "js-work-packet-1" &&
            packet.at("schema_versions").at("report") == "js-verifier-report-1",
            "schema.compatibility", "work packet schema version mismatch");
        for (const auto &[hash_name, content_name] : {std::pair<std::string, std::string>{"intent_sha256", "intent_manifest"},
                                                        {"artifact_sha256", "artifact_report"}}) {
            const bool has_hash = packet.contains(hash_name), has_content = packet.contains(content_name);
            demand(has_hash == has_content, "packet.unbound_component", "optional intent/artifact claim lacks its content payload");
            if (has_hash) demand(packet.at(hash_name).get<std::string>() == sha256(canonical_json(packet.at(content_name))),
                "binding.component", "work packet optional component hash mismatch");
        }
        Replay replay(fp, mf);
        if (mf.at("fingerprint_sha256") != report.at("input_sha256").at("fingerprint")) replay.add("binding.fingerprint", "fingerprint bytes do not match manifest SHA-256");
        if (mf.at("program_sha256") != report.at("input_sha256").at("program")) replay.add("binding.program", "final program bytes do not match manifest SHA-256");
        const std::set<std::string> checked {"syntax", "modal_state", "numeric", "build_volume", "tool_envelope", "axis_velocity",
            "configured_motion_limits", "volumetric_flow", "temperature_commands", "cold_extrusion", "extrusion_continuity",
            "object_boundaries", "fingerprint_binding", "program_integrity", "schema_compatibility"};
        const std::map<std::string, std::string> unproven {
            {"datum_protection", "No exact datum geometry or tolerance contract is supplied/implemented."},
            {"swept_collision", "Only a translated tool AABB against the build box is checked; deposited-part collision is unproven."},
            {"dependency_contracts", "G-code does not establish support/deposition dependencies."},
            {"contact_contracts", "No independent physical contact/bond evidence or calibrated model."},
            {"physical_machine_state", "Initial position, calibrated fingerprint, firmware semantics and maintained temperatures are trusted inputs; no telemetry."},
            {"dynamic_motion", "Requested speeds and configured limits checked; acceleration trajectories, jerk, resonance and step timing unproven."},
            {"artifact_binding", "No compiler ArtifactIR/contract payload is consumed; only final program and fingerprint bytes are bound."}};
        for (const auto &[property, reason] : unproven) replay.add("unproven." + property, reason, "warning");
        for (const auto &property : mf.at("required_properties"))
            if (!checked.contains(property.get<std::string>())) replay.add("rule.unproven", "required property is unimplemented: " + property.get<std::string>());
        try { replay.bounds(replay.position); } catch (const Failure &error) { replay.add(error.code, error.what()); }
        std::size_t cursor = 0;
        std::string last_nonempty;
        bool semantic_error = false;
        std::size_t first_replay_error = 0;
        while (cursor < program.size()) {
            replay.offset = cursor; ++replay.line;
            const auto line_start = cursor;
            const auto newline = program.find('\n', cursor);
            auto text = program.substr(cursor, (newline == std::string_view::npos ? program.size() : newline) - cursor);
            cursor = newline == std::string_view::npos ? program.size() : newline + 1;
            const auto line_end = cursor;
            text = trim(text);
            if (!text.empty()) last_nonempty = text;
            const auto semicolon = text.find(';');
            const auto instruction = trim(text.substr(0, semicolon));
            if (!instruction.empty()) {
                ++replay.commands;
                replay.command_bytes.emplace_back(program.substr(line_start, line_end - line_start));
                replay.command_lines.push_back(replay.line);
                replay.command_objects.push_back(replay.active);
                replay.command_tools.push_back(replay.tool);
            }
            try {
                demand(text.size() <= 1024 * 1024 && text.find('\0') == std::string_view::npos, "syntax.line", "oversized line or binary data");
                if (!instruction.empty()) {
                    demand(replay.declaration.empty(), "object.boundary", "unfinished header declaration before commands");
                    demand(replay.end_markers == 0, "program.trailing", "executable command after end marker");
                    replay.executed = true;
                    replay.command(parse(instruction));
                }
                if (semicolon != std::string_view::npos) replay.comment(text.substr(semicolon + 1));
            } catch (const Failure &error) {
                replay.add(error.code, error.what()); semantic_error = true;
                if (first_replay_error == 0) first_replay_error = replay.line;
            } catch (const std::exception &) {
                replay.add("syntax.metadata", "malformed structured metadata"); semantic_error = true;
                if (first_replay_error == 0) first_replay_error = replay.line;
            }
        }
        replay.offset = program.size();
        if (last_nonempty != mf.at("end_marker").get<std::string>() || replay.end_markers != 1 || program.empty() || program.back() != '\n')
            replay.add("program.truncated", "missing unique terminal marker or final newline");
        if (replay.motions == 0 && !semantic_error) replay.add("program.empty", "program contains no motion");
        if (!replay.active.empty() || !replay.declaration.empty() || replay.declared != replay.expected || replay.used != replay.expected || !replay.objects_info_seen)
            replay.add("object.boundary", "incomplete object declarations, usage, metadata or boundaries at EOF");
        if (replay.bed_target != 0 || std::any_of(replay.target.begin(), replay.target.end(), [](double value) { return value != 0; }))
            replay.add("shutdown.heaters", "heater target remains on at EOF");

        // Verify the immutable sidecar independently of compiler state. Command
        // ordinals refer to executable lines in the final byte stream.
        const auto total_lines = replay.line;
        const auto command_digest = [&](std::size_t first, std::size_t last) {
            std::string bytes;
            for (std::size_t i = first - 1; i < last; ++i) bytes += replay.command_bytes[i];
            return sha256(bytes);
        };
        std::map<std::string, std::pair<std::size_t, std::size_t>> path_ranges;
        std::set<std::string> path_ids;
        for (const auto &path : packet.at("paths")) {
            const auto id = path.at("id").get<std::string>();
            const auto first = path.at("command_start").get<std::size_t>(), last = path.at("command_end").get<std::size_t>();
            if (!path_ids.insert(id).second) replay.add("path.duplicate_id", "duplicate sidecar path identity");
            else path_ranges[id] = {first, last};
        }
        std::set<std::string> checked_path_ids;
        for (const auto &path : packet.at("paths")) {
            const auto id = path.at("id").get<std::string>();
            if (!checked_path_ids.insert(id).second) continue;
            const auto first = path.at("command_start").get<std::size_t>(), last = path.at("command_end").get<std::size_t>();
            if (!path_ids.contains(id) || path_ranges.at(id) != std::pair<std::size_t, std::size_t>{first, last}) continue;
            if (first == 0 || first > last || last > replay.command_bytes.size()) {
                replay.add("path.range", "sidecar path command range is outside final program");
                continue;
            }
            if (path.at("command_sha256") != command_digest(first, last))
                replay.add("path.digest", "sidecar path command digest mismatch", "error");
            std::set<std::string> labels;
            std::set<std::size_t> tools;
            for (std::size_t i = first - 1; i < last; ++i) {
                labels.insert(replay.command_objects[i]);
                tools.insert(replay.command_tools[i]);
            }
            if (labels.size() > 1 || (labels.size() == 1 && *labels.begin() != path.value("object", "")))
                replay.add("path.object_boundary", "sidecar path crosses or disagrees with an object boundary");
            if (path.contains("object") && labels.size() == 0)
                replay.add("path.object_identity", "sidecar path object identity is absent from final commands");
            if (path.contains("tool") && (tools.size() != 1 || *tools.begin() != path.at("tool").get<std::size_t>()))
                replay.add("path.tool", "sidecar path tool identity differs from final commands");
            for (const auto &predecessor : path.at("predecessors")) {
                const auto previous = predecessor.get<std::string>();
                const auto it = path_ranges.find(previous);
                if (it == path_ranges.end()) replay.add("path.predecessor", "sidecar predecessor is missing");
                else if (it->second.second >= first) replay.add("path.predecessor", "sidecar predecessor follows or overlaps dependent path");
            }
        }
        std::set<std::string> temporary_ids;
        std::map<std::string, std::pair<std::size_t, std::size_t>> temporary_ranges;
        for (const auto &temporary : packet.at("temporary_structures")) {
            const auto id = temporary.at("id").get<std::string>();
            if (!temporary_ids.insert(id).second) replay.add("temporary.duplicate_id", "duplicate temporary structure identity");
            const auto create = temporary.at("create_command").get<std::size_t>(), last = temporary.at("last_use_command").get<std::size_t>();
            temporary_ranges[id] = {create, last};
            if (create == 0 || create > last || last > replay.command_bytes.size()) replay.add("temporary.lifetime", "temporary structure lifetime is outside final program");
            if (temporary.contains("object")) {
                for (std::size_t i = create == 0 ? 0 : create - 1; i < std::min(last, replay.command_objects.size()); ++i)
                    if (!replay.command_objects[i].empty() && replay.command_objects[i] != temporary.at("object").get<std::string>()) {
                        replay.add("temporary.object", "temporary structure lifetime crosses object identity");
                        break;
                    }
            }
        }
        for (const auto &path : packet.at("paths")) {
            const auto first = path.at("command_start").get<std::size_t>(), last = path.at("command_end").get<std::size_t>();
            for (const auto &use : path.value("uses_temporary", Json::array())) {
                const auto it = temporary_ranges.find(use.get<std::string>());
                if (it == temporary_ranges.end() || first < it->second.first || last > it->second.second)
                    replay.add("temporary.use", "path uses a temporary structure outside its declared lifetime");
            }
        }
        const auto aabb_intersects = [](const V3 &a0, const V3 &a1, const V3 &b0, const V3 &b1) {
            for (std::size_t i = 0; i < 3; ++i) if (std::max(a0[i], a1[i]) < b0[i] - epsilon || std::min(a0[i], a1[i]) > b1[i] + epsilon) return false;
            return true;
        };
        const auto aabb_inside = [](const V3 &a0, const V3 &a1, const V3 &b0, const V3 &b1) {
            for (std::size_t i = 0; i < 3; ++i) if (std::min(a0[i], a1[i]) < b0[i] - epsilon || std::max(a0[i], a1[i]) > b1[i] + epsilon) return false;
            return true;
        };
        for (const auto &datum : packet.at("datums")) {
            const auto object = datum.at("object").get<std::string>();
            const auto pmin = datum.at("protected_min_mm").get<V3>(), pmax = datum.at("protected_max_mm").get<V3>();
            const auto emin = datum.at("permitted_min_mm").get<V3>(), emax = datum.at("permitted_max_mm").get<V3>();
            const auto radius = datum.at("max_bead_radius_mm").get<double>();
            if (!aabb_inside(pmin, pmax, emin, emax)) replay.add("datum.contract", "protected datum region is outside permitted envelope");
            if (datum.value("require_no_interlocking", 0) != 0 || datum.value("require_feature_identity", 0) != 0)
                replay.add("rule.unproven", "requested datum feature/interlocking property is not recoverable from final G-code", "error");
            for (const auto &segment : replay.depositions) {
                V3 expanded_min {}, expanded_max {};
                for (std::size_t i = 0; i < 3; ++i) {
                    expanded_min[i] = std::min(segment.start[i], segment.end[i]) - radius;
                    expanded_max[i] = std::max(segment.start[i], segment.end[i]) + radius;
                }
                if (!aabb_intersects(expanded_min, expanded_max, pmin, pmax)) continue;
                replay.line = segment.line;
                if (segment.object != object) replay.add("datum.object", "deposition intersecting datum has the wrong object identity");
                if (!aabb_inside(expanded_min, expanded_max, emin, emax)) replay.add("datum.envelope", "deposition bead sweep leaves the permitted datum envelope");
                if (datum.contains("planar_z_mm") && (std::abs(segment.start[2] - datum.at("planar_z_mm").get<double>()) > epsilon ||
                    std::abs(segment.end[2] - datum.at("planar_z_mm").get<double>()) > epsilon))
                    replay.add("datum.planarity", "deposition intersecting datum is outside the bound planar datum");
            }
        }
        replay.line = total_lines;
        std::stable_sort(replay.findings.begin(), replay.findings.end(), [](const Json &a, const Json &b) {
            return std::tuple(a.at("line").get<std::size_t>(), a.at("code").get<std::string>(), a.at("message").get<std::string>()) <
                   std::tuple(b.at("line").get<std::size_t>(), b.at("code").get<std::string>(), b.at("message").get<std::string>());
        });
        const bool accepted = std::none_of(replay.findings.begin(), replay.findings.end(), [](const Json &f) { return f.at("severity") == "error"; });
        report["accepted"] = accepted;
        report["findings"] = replay.findings;
        report["lines_scanned"] = replay.line;
        report["commands_seen"] = replay.commands;
        report["replay_complete"] = !semantic_error;
        report["first_replay_error_line"] = first_replay_error == 0 ? Json(nullptr) : Json(first_replay_error);
        report["final_state"] = replay.state();
        report["final_state_trusted"] = accepted;
        report["checked_properties"] = checked;
        report["unproven_properties"] = unproven;
        report["metrics"] = {{"linear_commands", replay.motions}, {"peak_requested_feedrate_mm_s", replay.peak_feed}, {"peak_positive_drive_flow_mm3_s", replay.peak_flow}};
    } catch (const Failure &error) { input_error(error.code, error.what()); }
    catch (const std::exception &error) { input_error("input.invalid", error.what()); }
    return report;
}
}
