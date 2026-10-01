#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "BreakingApart/BreakingApart.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <file> [--nc]\n";
        return 1;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open: " << argv[1] << "\n";
        return 1;
    }

    const std::string data{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};

    const bool isNC = (argc > 2 && std::string(argv[2]) == "--nc");

    std::vector<std::vector<size_t>> positions;
    BoundariesFinder(data, positions, isNC);

    const auto &cuts = positions.back();
    if (cuts.empty()) {
        std::cout << "no chunk produced\n";
        return 0;
    }

    std::size_t prev = 0;
    std::size_t minChunk = std::numeric_limits<std::size_t>::max();
    std::size_t maxChunk = 0;
    for (const std::size_t cut : cuts) {
        const std::size_t len = cut - prev;
        minChunk = std::min(minChunk, len);
        maxChunk = std::max(maxChunk, len);
        prev = cut;
    }

    const double avg = static_cast<double>(data.size()) / static_cast<double>(cuts.size());
    std::cout << "file size : " << data.size() << "\n"
              << "chunks    : " << cuts.size() << "\n"
              << "min chunk : " << minChunk << "\n"
              << "avg chunk : " << avg << "\n"
              << "max chunk : " << maxChunk << "\n";
    return 0;
}
