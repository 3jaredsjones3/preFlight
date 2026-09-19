#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace JSlice::Verification {
using Json = nlohmann::json;
// Inputs are immutable bytes. No repair, generator, IR or machine control API.
// Reports distinguish acceptance under the supplied assumptions from unproven
// physical properties. Line 0 denotes an input/manifest rather than G-code.
Json verify(std::string_view program, std::string_view fingerprint, std::string_view manifest,
    std::string_view work_packet);
std::string sha256(std::string_view bytes);
Json strict_json(std::string_view bytes);
std::string canonical_json(const Json &value);
void validate_schema(const Json &value, const Json &schema, const std::string &path = "$");
}
