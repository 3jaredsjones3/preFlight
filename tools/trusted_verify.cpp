#include "Verifier.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

static std::string read(const char *path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > 256 * 1024 * 1024)
        throw std::runtime_error("unreadable or oversized input");
    input.seekg(0);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main(int argc, char **argv)
{
    try {
        if (argc != 4) throw std::runtime_error("usage: jslice_verify PROGRAM.gcode MACHINE.json MANIFEST.json");
        const auto report = JSlice::Verification::verify(read(argv[1]), read(argv[2]), read(argv[3]));
        std::cout << report.dump(2) << '\n';
        return report.at("accepted").get<bool>() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cout << JSlice::Verification::Json({{"schema_version", "js-verifier-report-1"}, {"accepted", false},
            {"findings", {{{"line", 0}, {"severity", "error"}, {"code", "input.io"}, {"message", error.what()}}}}}).dump(2) << '\n';
        return 2;
    }
}
