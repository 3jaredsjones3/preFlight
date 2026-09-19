#include "Verifier.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace JSlice::Verification {
std::string sha256(std::string_view bytes)
{
    unsigned char digest[32] {};
#ifdef _WIN32
    if (bytes.size() > std::numeric_limits<ULONG>::max()) throw std::runtime_error("hash input too large");
    BCRYPT_ALG_HANDLE algorithm {};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA-256 provider unavailable");
    ULONG object_length = 0, written = 0;
    auto status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length),
        sizeof(object_length), &written, 0);
    std::vector<unsigned char> object(object_length);
    BCRYPT_HASH_HANDLE hash {};
    if (status >= 0) status = BCryptCreateHash(algorithm, &hash, object.data(), object_length, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.data())), static_cast<ULONG>(bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest, 32, 0);
    if (hash != nullptr) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("SHA-256 failed");
#else
    unsigned int length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_sha256(), nullptr) != 1 || length != 32)
        throw std::runtime_error("SHA-256 failed");
#endif
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto byte : digest) result << std::setw(2) << static_cast<unsigned int>(byte);
    return result.str();
}

Json strict_json(std::string_view bytes)
{
    if (bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("JSON input too large");
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &value) {
        if (depth > 64) throw std::runtime_error("JSON nesting too deep");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("duplicate JSON key");
        return true;
    });
}

namespace {
Json canonical_value(const Json &value)
{
    if (value.is_object()) {
        Json result = Json::object();
        std::vector<std::string> keys;
        keys.reserve(value.size());
        for (const auto &[key, ignored] : value.items()) {
            (void)ignored;
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        for (const auto &key : keys) result[key] = canonical_value(value.at(key));
        return result;
    }
    if (value.is_array()) {
        Json result = Json::array();
        for (const auto &item : value) result.push_back(canonical_value(item));
        return result;
    }
    return value;
}
}

std::string canonical_json(const Json &value)
{
    return canonical_value(value).dump(-1, ' ', false, Json::error_handler_t::strict);
}

// Deliberately limited to the keywords used by the two compiled-in v1 schemas.
// A schema author cannot add a keyword that silently receives no enforcement.
void validate_schema(const Json &v, const Json &s, const std::string &path)
{
    static const std::set<std::string> keywords {"$schema", "$id", "type", "const", "properties", "required",
        "additionalProperties", "items", "minItems", "maxItems", "minimum", "maximum", "exclusiveMinimum",
        "minLength", "maxLength", "pattern"};
    const auto fail = [&](const std::string &reason) { throw std::runtime_error(path + ": " + reason); };
    for (const auto &[key, ignored] : s.items()) {
        (void)ignored;
        if (!keywords.contains(key)) fail("unsupported embedded schema keyword " + key);
    }
    if (s.contains("const") && v != s.at("const")) fail("unsupported schema version or constant");
    if (s.contains("type")) {
        const auto type = s.at("type").get<std::string>();
        const bool valid = type == "object" ? v.is_object() : type == "array" ? v.is_array() :
            type == "string" ? v.is_string() : type == "number" ? v.is_number() :
            type == "integer" ? v.is_number() && std::isfinite(v.get<double>()) && std::floor(v.get<double>()) == v.get<double>() : false;
        if (!valid) fail("expected " + type);
    }
    if (v.is_object()) {
        for (const auto &key : s.value("required", Json::array()))
            if (!v.contains(key.get<std::string>())) fail("missing field " + key.get<std::string>());
        for (const auto &[key, value] : v.items()) {
            if (s.contains("properties") && s.at("properties").contains(key))
                validate_schema(value, s.at("properties").at(key), path + "." + key);
            else if (!s.value("additionalProperties", true)) fail("unknown field " + key);
        }
    } else if (v.is_array()) {
        if (v.size() < s.value("minItems", std::size_t(0)) || v.size() > s.value("maxItems", std::numeric_limits<std::size_t>::max()))
            fail("array length");
        if (s.contains("items")) for (std::size_t i = 0; i < v.size(); ++i)
            validate_schema(v[i], s.at("items"), path + "[" + std::to_string(i) + "]");
    } else if (v.is_number()) {
        const double n = v.get<double>();
        if (!std::isfinite(n)) fail("non-finite number");
        if (s.contains("minimum") && n < s.at("minimum").get<double>()) fail("below minimum");
        if (s.contains("maximum") && n > s.at("maximum").get<double>()) fail("above maximum");
        if (s.contains("exclusiveMinimum") && n <= s.at("exclusiveMinimum").get<double>()) fail("below exclusive minimum");
    } else if (v.is_string()) {
        const auto value = v.get<std::string>();
        const auto length = static_cast<std::size_t>(std::count_if(value.begin(), value.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; }));
        if (length < s.value("minLength", std::size_t(0)) || length > s.value("maxLength", std::numeric_limits<std::size_t>::max())) fail("string length");
        if (s.contains("pattern") && !std::regex_match(value, std::regex(s.at("pattern").get<std::string>()))) fail("string pattern");
    }
}
}
