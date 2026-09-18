#include "libslic3r/Predictive/PredictiveCompiler.hpp"
#include "libslic3r/Predictive/SupportIR.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Slic3r::Predictive;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

BeadPath make_path(PathId id, double y, std::vector<PathId> dependencies = {})
{
    BeadPath path;
    path.id = id;
    path.role = PathRole::Perimeter;
    path.dependencies = std::move(dependencies);
    path.provenance = {"smoke-cube", "wall", "legacy_adapter", 0, std::nullopt};
    path.material_id = "PLA";
    path.samples = {
        {{10.0, y, 0.2}, 0.45, 0.2, 40.0, 0.9},
        {{30.0, y, 0.2}, 0.45, 0.2, 40.0, 1.8},
        {{30.0, y + 2.0, 0.2}, 0.45, 0.2, 30.0, 0.18}};
    return path;
}

MachineFingerprint make_machine()
{
    MachineFingerprint machine;
    machine.id = "smoke-machine";
    machine.version = "1";
    machine.content_hash = "sha256:smoke";
    machine.build_volume = {{0.0, 0.0, 0.0}, {220.0, 220.0, 220.0}};
    machine.tool_envelope = {10.0, 0.0, 30.0};
    machine.modes.push_back({40.0, 0.08, 0.000001});
    return machine;
}

void test_compiler_pipeline()
{
    CompilationInput input;
    input.machine = make_machine();
    input.bead_graph.paths.push_back(make_path(1, 10.0));
    input.bead_graph.paths.push_back(make_path(2, 12.0, {1}));
    input.bead_graph.paths[1].contacts.push_back(
        {1, ContactContract::Intent::StructuralBond, 0.0, 1.0, 0.1, 0.0, false});
    input.fields.samples.push_back({{20.0, 10.0, 0.2}, 0.25, {1.0, 0.0, 0.0}, 0.5, 0.45, 1.0});
    input.fields.datums.push_back(
        {1, "mating surface", {{9.0, 9.0, 0.0}, {31.0, 15.0, 1.0}},
         1.0, 1.0, false, false, true});

    CompilerConfig config;
    config.require_certification = true;
    config.enable_pressure_release = false;
    PredictiveCompiler compiler(config);
    CompilationResult result = compiler.compile(input);

    require(result.report.ok(), "baseline predictive compilation should certify");
    require(result.schedule.size() == 2 && result.schedule[0] == 1 && result.schedule[1] == 2,
            "dependencies must be preserved by scheduling");
    require(!result.timed_moves.empty(), "time parameterization should produce moves");
    require(!result.predicted_artifact.cells.empty(), "forward model should predict deposited cells");
    require(result.gcode.find("machine_fingerprint_hash") != std::string::npos,
            "G-code should carry fingerprint provenance");
}

void test_graph_rejection()
{
    BeadGraphIR graph;
    graph.paths.push_back(make_path(1, 10.0, {2}));
    graph.paths.push_back(make_path(2, 12.0, {1}));
    require(!validate_bead_graph(graph).ok(), "dependency cycles must be rejected");

    graph.paths[0].dependencies.clear();
    graph.paths[1].dependencies.clear();
    graph.paths[1].contacts.push_back(
        {1, ContactContract::Intent::DesignedFracture, 0.8, 0.2, 1.0, 0.1, true});
    require(!validate_bead_graph(graph).ok(), "inverted bond contracts must be rejected");
}

void test_support_contracts()
{
    SupportGraphIR support;
    support.nodes = {
        {1, SupportNodeKind::BedAnchor, {10.0, 10.0, 0.0}, true, std::nullopt},
        {2, SupportNodeKind::Contact, {10.0, 10.0, 8.0}, false,
         ContactContract {1, ContactContract::Intent::DesignedFracture,
                          0.15, 0.35, 0.5, 0.15, true}}};
    support.members = {
        {1, 1, 2, SupportMemberKind::StackedStrut, SupportDuty::ReleaseInterface,
         3.0, 120.0, 0.8, 10.0, 0.15, true, {}, {"smoke", "support", "", 0, std::nullopt}}};

    SupportValidationResult valid = validate_support_graph(support);
    require(valid.report.ok() && valid.drawable_order == std::vector<SupportMemberId> {1},
            "seeded support graph should be drawable deterministically");

    ValidationReport lowering_report;
    BeadGraphIR lowered = lower_support_graph(support, 100, lowering_report);
    require(lowering_report.ok() && lowered.paths.size() == 1,
            "qualified stacked support should lower to a bead path");

    support.nodes[0].kind = SupportNodeKind::Contact;
    support.nodes[0].initially_available = false;
    require(!validate_support_graph(support).report.ok(),
            "unseeded support components must be rejected");

    support.nodes[0].kind = SupportNodeKind::BedAnchor;
    support.nodes[0].initially_available = true;
    support.members[0].kind = SupportMemberKind::ArchCandidate;
    lowering_report = {};
    lowered = lower_support_graph(support, 100, lowering_report);
    require(!lowering_report.ok() && lowered.paths.empty(),
            "unqualified free-space primitives must remain gated");
}

void test_bounded_live_policy()
{
    const ClosedLoopDecision uncertain = decide_closed_loop_response(
        {VisionObservation::Kind::DetachedObject, 1, 0.4, 1.0}, 0.8, 0.1);
    require(uncertain.action == ClosedLoopDecision::Action::Continue,
            "low-confidence observations must not alter the print");

    const ClosedLoopDecision detached = decide_closed_loop_response(
        {VisionObservation::Kind::DetachedObject, 1, 0.95, 1.0}, 0.8, 0.1);
    require(detached.action == ClosedLoopDecision::Action::ExcludeObject,
            "confirmed detached objects should use exclusion rather than ejection");
}

} // namespace

int main()
{
    test_compiler_pipeline();
    test_graph_rejection();
    test_support_contracts();
    test_bounded_live_policy();
    std::cout << "J-Slice predictive core smoke checks passed\n";
    return EXIT_SUCCESS;
}
