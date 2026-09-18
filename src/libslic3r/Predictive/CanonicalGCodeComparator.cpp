#include "CanonicalGCodeComparator.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace Slic3r::Predictive {
namespace {
std::string trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return std::string(s.substr(first, s.find_last_not_of(" \t\r") - first + 1));
}
struct Line {
    std::size_t number;
    std::string raw, comment, command;
    std::optional<std::map<char, double>> words;
};
Line parse(std::string raw, std::size_t number)
{
    Line line {number, std::move(raw), {}, {}, std::nullopt};
    const auto semicolon = line.raw.find(';');
    const std::string code = trim(std::string_view(line.raw).substr(0, semicolon));
    if (semicolon != std::string::npos) line.comment = line.raw.substr(semicolon);
    if (code.size() < 2 || (code[0] != 'G' && code[0] != 'M' && code[0] != 'T')) return line;
    std::size_t pos = 1;
    while (pos < code.size() && code[pos] >= '0' && code[pos] <= '9') ++pos;
    if (pos == 1) return line;
    int command_number = 0;
    const auto converted = std::from_chars(code.data() + 1, code.data() + pos, command_number);
    if (converted.ec != std::errc {}) return line;
    line.command = code[0] + std::to_string(command_number);
    const std::vector<std::string> known {"G0", "G1", "G2", "G3", "G4", "G20", "G21", "G90", "G91", "G92",
        "M82", "M83", "M104", "M109", "M106", "M107", "M140", "M190", "M201", "M203", "M204", "M205", "M220", "M221"};
    if (code[0] != 'T' && std::find(known.begin(), known.end(), line.command) == known.end()) return line;
    std::map<char, double> words;
    while (pos < code.size()) {
        if (code[pos] == ' ' || code[pos] == '\t') { ++pos; continue; }
        const char key = code[pos++];
        // Multiple commands in one block have dialect-dependent ordering rules.
        if (key < 'A' || key > 'Z' || key == 'G' || key == 'M' || key == 'N' || words.count(key) != 0) return line;
        const auto start = pos;
        if (pos < code.size() && (code[pos] == '+' || code[pos] == '-')) ++pos;
        bool digit = false, decimal = false;
        while (pos < code.size()) {
            const char c = code[pos];
            if (c >= '0' && c <= '9') { digit = true; ++pos; }
            else if (c == '.' && !decimal) { decimal = true; ++pos; }
            else break;
        }
        if (!digit) return line;
        auto begin = code.data() + start;
        if (*begin == '+') ++begin;
        double value = 0.0;
        const auto result = std::from_chars(begin, code.data() + pos, value, std::chars_format::fixed);
        if (result.ec != std::errc {} || result.ptr != code.data() + pos || !std::isfinite(value)) return line;
        words.emplace(key, value);
    }
    line.words = std::move(words);
    return line;
}
std::vector<Line> canonical(std::string_view input, const std::vector<std::string> &prefixes)
{
    std::istringstream stream {std::string(input)};
    std::vector<Line> result;
    std::string raw;
    std::size_t number = 0;
    while (std::getline(stream, raw)) {
        ++number;
        // Only line-ending CR is discarded for opaque lines.
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const auto stripped = trim(raw);
        if (stripped.empty()) continue;
        bool ignored = false;
        for (const auto &prefix : prefixes)
            if (stripped.starts_with(prefix)) ignored = true;
        if (!ignored) result.push_back(parse(std::move(raw), number));
    }
    return result;
}
} // namespace

GCodeComparison compare_gcode(std::string_view baseline, std::string_view candidate,
                             const std::vector<std::string> &prefixes)
{
    for (const auto &prefix : prefixes)
        if (prefix.size() < 2 || prefix.front() != ';')
            throw std::invalid_argument("Volatile prefixes must name specific full-line comments");
    if (baseline == candidate) return {true, true, 0, 0, "byte-identical"};
    const auto left = canonical(baseline, prefixes), right = canonical(candidate, prefixes);
    for (std::size_t i = 0; i < std::max(left.size(), right.size()); ++i) {
        if (i >= left.size() || i >= right.size())
            return {false, false, i < left.size() ? left[i].number : 0, i < right.size() ? right[i].number : 0, "command/comment count differs"};
        const auto &a = left[i], &b = right[i];
        const bool same = a.words && b.words ? a.command == b.command && a.words == b.words && a.comment == b.comment : a.raw == b.raw;
        if (!same) return {false, false, a.number, b.number, "command, parameter, or preserved metadata differs"};
    }
    return {true, false, 0, 0, "canonical command streams match"};
}
} // namespace Slic3r::Predictive
