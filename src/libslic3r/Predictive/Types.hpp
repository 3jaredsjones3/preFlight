#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Predictive {

using PathId = std::uint64_t;
using DatumId = std::uint64_t;
using SupportNodeId = std::uint64_t;
using SupportMemberId = std::uint64_t;

struct Vec3 {
    double x {0.0};
    double y {0.0};
    double z {0.0};

    Vec3 operator+(const Vec3 &rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    Vec3 operator-(const Vec3 &rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    Vec3 operator*(double scale) const { return {x * scale, y * scale, z * scale}; }
    Vec3 operator/(double scale) const { return {x / scale, y / scale, z / scale}; }
};

inline double dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double squared_norm(const Vec3 &value) { return dot(value, value); }
inline double norm(const Vec3 &value) { return std::sqrt(squared_norm(value)); }
inline double distance(const Vec3 &a, const Vec3 &b) { return norm(a - b); }

inline Vec3 normalized(const Vec3 &value)
{
    const double magnitude = norm(value);
    return magnitude > 1e-12 ? value / magnitude : Vec3 {1.0, 0.0, 0.0};
}

struct Aabb3 {
    Vec3 minimum {
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    Vec3 maximum {
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};

    bool valid() const
    {
        return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
    }

    void include(const Vec3 &point)
    {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }

    Aabb3 expanded(double amount) const
    {
        return {{minimum.x - amount, minimum.y - amount, minimum.z - amount},
                {maximum.x + amount, maximum.y + amount, maximum.z + amount}};
    }

    bool contains(const Vec3 &point) const
    {
        return point.x >= minimum.x && point.x <= maximum.x && point.y >= minimum.y &&
               point.y <= maximum.y && point.z >= minimum.z && point.z <= maximum.z;
    }

    bool intersects(const Aabb3 &other) const
    {
        return minimum.x <= other.maximum.x && maximum.x >= other.minimum.x &&
               minimum.y <= other.maximum.y && maximum.y >= other.minimum.y &&
               minimum.z <= other.maximum.z && maximum.z >= other.minimum.z;
    }
};

enum class Severity { Information, Warning, Error };

struct ValidationIssue {
    Severity severity {Severity::Error};
    std::string code;
    std::string message;
    std::optional<PathId> path_id;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    bool ok() const
    {
        return std::none_of(issues.begin(), issues.end(), [](const ValidationIssue &issue) {
            return issue.severity == Severity::Error;
        });
    }

    void add(Severity severity, std::string code, std::string message,
             std::optional<PathId> path_id = std::nullopt)
    {
        issues.push_back({severity, std::move(code), std::move(message), path_id});
    }

    void merge(const ValidationReport &other)
    {
        issues.insert(issues.end(), other.issues.begin(), other.issues.end());
    }
};

struct Provenance {
    std::string source_object;
    std::string source_feature;
    std::string generating_pass;
    std::optional<std::uint32_t> source_layer;
    std::optional<PathId> parent_path;
};

} // namespace Slic3r::Predictive
