#include "libslic3r/Predictive/CanonicalGCodeComparator.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc, char **argv)
{
    try {
        if (argc < 3) throw std::runtime_error("usage: predictive_compare_gcode BASELINE CANDIDATE [VOLATILE_COMMENT_PREFIX ...]");
        const auto read = [](const char *path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error(std::string("Cannot read ") + path);
            return std::string(std::istreambuf_iterator<char>(file), {});
        };
        std::vector<std::string> prefixes;
        for (int i = 3; i < argc; ++i) prefixes.emplace_back(argv[i]);
        const auto result = Slic3r::Predictive::compare_gcode(read(argv[1]), read(argv[2]), prefixes);
        std::cout << result.reason << " (baseline line " << result.baseline_line << ", candidate line " << result.candidate_line << ")\n";
        return result.equivalent ? 0 : 1;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 2; }
}
