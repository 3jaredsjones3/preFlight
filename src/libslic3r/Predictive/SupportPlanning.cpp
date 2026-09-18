#include "SupportIR.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r::Predictive {

SupportValidationResult validate_support_graph(const SupportGraphIR &graph)
{
    SupportValidationResult result;
    std::unordered_set<SupportNodeId> node_ids;
    std::unordered_set<SupportMemberId> member_ids;

    for (const SupportNode &node : graph.nodes) {
        if (node.id == 0 || !node_ids.insert(node.id).second)
            result.report.add(Severity::Error, "support.invalid_node_id",
                              "Support node identifiers must be unique and nonzero.");
        if (node.contact && !node.contact->valid())
            result.report.add(Severity::Error, "support.invalid_contact",
                              "Support node has an invalid contact contract.");
    }

    for (const SupportMember &member : graph.members) {
        if (member.id == 0 || !member_ids.insert(member.id).second)
            result.report.add(Severity::Error, "support.invalid_member_id",
                              "Support member identifiers must be unique and nonzero.");
        if (graph.find_node(member.from) == nullptr || graph.find_node(member.to) == nullptr)
            result.report.add(Severity::Error, "support.missing_endpoint",
                              "Support member references a missing endpoint.");
        if (member.diameter_mm <= 0.0 || member.maximum_unsupported_span_mm <= 0.0 ||
            member.service_load_n < 0.0 || member.required_lifetime_s < 0.0 ||
            member.scar_budget_mm < 0.0)
            result.report.add(Severity::Error, "support.invalid_member_contract",
                              "Support member has invalid geometric or service constraints.");
    }

    for (const SupportMember &member : graph.members) {
        for (SupportMemberId dependency : member.dependencies) {
            if (!member_ids.contains(dependency))
                result.report.add(Severity::Error, "support.missing_dependency",
                                  "Support member references a missing dependency.");
        }
    }

    if (!result.report.ok())
        return result;

    std::unordered_set<SupportNodeId> available_nodes;
    for (const SupportNode &node : graph.nodes) {
        if (node.initially_available || node.kind == SupportNodeKind::BedAnchor ||
            node.kind == SupportNodeKind::PermittedPartAnchor)
            available_nodes.insert(node.id);
    }

    std::unordered_set<SupportMemberId> drawn;
    while (drawn.size() < graph.members.size()) {
        const SupportMember *selected = nullptr;
        for (const SupportMember &member : graph.members) {
            if (drawn.contains(member.id))
                continue;
            const bool dependencies_ready = std::all_of(
                member.dependencies.begin(), member.dependencies.end(),
                [&drawn](SupportMemberId id) { return drawn.contains(id); });
            const bool seeded = available_nodes.contains(member.from) ||
                                available_nodes.contains(member.to);
            if (!dependencies_ready || !seeded)
                continue;
            if (selected == nullptr || member.id < selected->id)
                selected = &member;
        }

        if (selected == nullptr) {
            result.report.add(Severity::Error, "support.not_drawable",
                              "Support graph contains an unseeded component or dependency cycle.");
            break;
        }

        const SupportNode *from = graph.find_node(selected->from);
        const SupportNode *to = graph.find_node(selected->to);
        if (from != nullptr && to != nullptr &&
            distance(from->position, to->position) > selected->maximum_unsupported_span_mm) {
            result.report.add(Severity::Error, "support.span_exceeded",
                              "Support member exceeds its qualified unsupported span.");
            break;
        }

        drawn.insert(selected->id);
        result.drawable_order.push_back(selected->id);
        available_nodes.insert(selected->from);
        available_nodes.insert(selected->to);
    }
    return result;
}

BeadGraphIR lower_support_graph(const SupportGraphIR &graph, PathId first_path_id,
                                ValidationReport &report)
{
    BeadGraphIR output;
    const SupportValidationResult validation = validate_support_graph(graph);
    report.merge(validation.report);
    if (!validation.report.ok())
        return output;

    std::unordered_map<SupportMemberId, const SupportMember *> members;
    for (const SupportMember &member : graph.members)
        members[member.id] = &member;

    std::unordered_map<SupportMemberId, PathId> member_paths;
    PathId next_path_id = first_path_id;

    for (SupportMemberId member_id : validation.drawable_order) {
        const SupportMember &member = *members.at(member_id);
        if (member.kind == SupportMemberKind::AxialStrand ||
            member.kind == SupportMemberKind::ArchCandidate) {
            report.add(Severity::Error, "support.experimental_primitive",
                       "Free-space strands and arches require a qualified physical process envelope.");
            continue;
        }

        const SupportNode *from = graph.find_node(member.from);
        const SupportNode *to = graph.find_node(member.to);
        if (from == nullptr || to == nullptr)
            continue;

        BeadPath path;
        path.id = next_path_id++;
        path.role = member.duty == SupportDuty::ReleaseInterface
                        ? PathRole::SupportInterface
                        : PathRole::Support;
        path.provenance = member.provenance;
        path.provenance.generating_pass = "support_graph_lowering";
        path.samples = {
            {from->position, member.diameter_mm, member.diameter_mm * 0.5, 20.0, 0.0},
            {to->position, member.diameter_mm, member.diameter_mm * 0.5, 20.0, 0.0}};
        for (SupportMemberId dependency : member.dependencies) {
            const auto found = member_paths.find(dependency);
            if (found != member_paths.end())
                path.dependencies.push_back(found->second);
        }
        if (to->contact)
            path.contacts.push_back(*to->contact);
        if (from->contact)
            path.contacts.push_back(*from->contact);
        member_paths[member.id] = path.id;
        output.paths.push_back(std::move(path));
    }

    return output;
}

} // namespace Slic3r::Predictive
