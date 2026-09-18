#pragma once

#include "Types.hpp"

#include <string>
#include <vector>

namespace Slic3r::Predictive {

struct AxisLimits {
    double maximum_speed_mm_s {300.0};
    double maximum_acceleration_mm_s2 {5000.0};
    double maximum_jerk_mm_s3 {50000.0};
};

struct ResonanceMode {
    double frequency_hz {0.0};
    double damping_ratio {0.1};
    double displacement_gain_mm_per_mm_s2 {0.0};
};

struct ExtrusionLimits {
    double maximum_volumetric_flow_mm3_s {15.0};
    double pressure_time_constant_s {0.05};
    double minimum_extrusion_speed_mm_s {0.1};
};

struct ToolEnvelope {
    double radius_mm {25.0};
    double below_reference_mm {0.0};
    double above_reference_mm {40.0};
};

struct MachineFingerprint {
    std::string id;
    std::string version;
    std::string content_hash;
    AxisLimits x_axis;
    AxisLimits y_axis;
    AxisLimits z_axis {30.0, 500.0, 5000.0};
    ExtrusionLimits extrusion;
    ToolEnvelope tool_envelope;
    Aabb3 build_volume {{0.0, 0.0, 0.0}, {220.0, 220.0, 220.0}};
    std::vector<ResonanceMode> modes;
};

} // namespace Slic3r::Predictive
