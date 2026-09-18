#include "LegacyExtrusionAdapter.hpp"

namespace Slic3r::Predictive {
std::uint16_t legacy_role_bits(ExtrusionRole role)
{
    std::uint16_t result = 0;
    // Capture all 16 bits, including future modifiers unknown to this adapter.
    for (unsigned int bit = 0; bit < 16; ++bit)
        if (role.has(static_cast<ExtrusionRoleModifier>(bit)))
            result = static_cast<std::uint16_t>(result | (1u << bit));
    return result;
}

PathRole legacy_path_role(std::uint16_t bits)
{
    const auto has = [bits](ExtrusionRoleModifier bit) {
        return (bits & (1u << static_cast<unsigned int>(bit))) != 0;
    };
    if (has(ExtrusionRoleModifier::Wipe)) return PathRole::Wipe;
    if (has(ExtrusionRoleModifier::Skirt)) return PathRole::SkirtBrim;
    if (has(ExtrusionRoleModifier::Support))
        return has(ExtrusionRoleModifier::External) ? PathRole::SupportInterface : PathRole::Support;
    if (has(ExtrusionRoleModifier::Bridge)) return PathRole::Bridge;
    if (has(ExtrusionRoleModifier::Perimeter))
        return has(ExtrusionRoleModifier::External) ? PathRole::ExternalPerimeter : PathRole::Perimeter;
    if (has(ExtrusionRoleModifier::Thin)) return PathRole::GapFill;
    if (has(ExtrusionRoleModifier::Ironing)) return PathRole::Ironing;
    if (has(ExtrusionRoleModifier::Infill)) {
        if (has(ExtrusionRoleModifier::External)) return PathRole::TopSurface;
        return has(ExtrusionRoleModifier::Solid) ? PathRole::SolidInfill : PathRole::Infill;
    }
    return PathRole::Unknown;
}
} // namespace Slic3r::Predictive
