#pragma once

#include "Types.hpp"

#include <string>
#include <vector>

namespace Slic3r::Predictive {

struct FieldSample {
    Vec3 position;
    double layer_coordinate {0.0};
    Vec3 preferred_direction {1.0, 0.0, 0.0};
    double relative_density {1.0};
    double bead_width_mm {0.4};
    double confidence {1.0};
};

struct FunctionalDatum {
    DatumId id {0};
    std::string name;
    Aabb3 protected_region;
    double positional_tolerance_mm {0.1};
    double surface_tolerance_mm {0.1};
    bool allow_nonplanar_warp {false};
    bool allow_interlock_deformation {false};
    bool allow_dynamic_compensation {true};
};

struct GeometryFieldIR {
    std::vector<FieldSample> samples;
    std::vector<FunctionalDatum> datums;

    const FunctionalDatum *datum_at(const Vec3 &point) const
    {
        for (const FunctionalDatum &datum : datums)
            if (datum.protected_region.contains(point))
                return &datum;
        return nullptr;
    }
};

} // namespace Slic3r::Predictive
