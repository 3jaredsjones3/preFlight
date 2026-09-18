#pragma once

#include "ToolpathIR.hpp"

#include <optional>
#include <vector>

namespace Slic3r::Predictive {

enum class SupportDuty {
    SagArrest,
    DepositionReaction,
    ThermalAnchor,
    DynamicBrace,
    StabilityBrace,
    ReleaseInterface
};

enum class SupportNodeKind {
    BedAnchor,
    PermittedPartAnchor,
    Contact,
    SeedProp,
    TransitJoint
};

enum class SupportMemberKind {
    AxialStrand,
    StackedStrut,
    Bridge,
    ArchCandidate,
    InterfaceStrand
};

struct SupportNode {
    SupportNodeId id {0};
    SupportNodeKind kind {SupportNodeKind::Contact};
    Vec3 position;
    bool initially_available {false};
    std::optional<ContactContract> contact;
};

struct SupportMember {
    SupportMemberId id {0};
    SupportNodeId from {0};
    SupportNodeId to {0};
    SupportMemberKind kind {SupportMemberKind::StackedStrut};
    SupportDuty duty {SupportDuty::SagArrest};
    double service_load_n {0.0};
    double required_lifetime_s {0.0};
    double diameter_mm {0.8};
    double maximum_unsupported_span_mm {10.0};
    double scar_budget_mm {0.0};
    bool requires_removal_access {false};
    std::vector<SupportMemberId> dependencies;
    Provenance provenance;
};

struct SupportGraphIR {
    std::vector<SupportNode> nodes;
    std::vector<SupportMember> members;

    const SupportNode *find_node(SupportNodeId id) const
    {
        for (const SupportNode &node : nodes)
            if (node.id == id)
                return &node;
        return nullptr;
    }
};

struct SupportValidationResult {
    ValidationReport report;
    std::vector<SupportMemberId> drawable_order;
};

SupportValidationResult validate_support_graph(const SupportGraphIR &graph);
BeadGraphIR lower_support_graph(const SupportGraphIR &graph, PathId first_path_id,
                                ValidationReport &report);

} // namespace Slic3r::Predictive
