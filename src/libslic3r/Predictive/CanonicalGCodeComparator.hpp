#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Predictive {
struct GCodeComparison {
    bool equivalent {false};
    bool byte_identical {false};
    std::size_t baseline_line {0}, candidate_line {0};
    std::string reason;
};
// Conservative regression comparator, NOT the M1.5 safety verifier. Keeps command
// order and modal transitions. Unknown commands and semantic comments are opaque.
// Only callers may explicitly allow volatile full-line comment prefixes.
GCodeComparison compare_gcode(std::string_view baseline, std::string_view candidate,
                             const std::vector<std::string> &volatile_comment_prefixes = {});
} // namespace Slic3r::Predictive
