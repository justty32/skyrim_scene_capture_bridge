#include "CatalogFile.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error(
                "usage: catalog_compatibility_probe <scene-catalog.json> <resolved-load-order.txt>");

        const auto loaded = CatalogFile::TryLoad(argv[1]);
        if (!loaded.document) throw std::runtime_error(loaded.status);

        std::ifstream input(argv[2]);
        if (!input) throw std::runtime_error("cannot read resolved load order");
        std::vector<CatalogFile::RuntimeSource> runtime;
        for (std::string line; std::getline(input, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            runtime.push_back({std::filesystem::path(line).filename().string()});
        }

        const auto compatibility = CatalogFile::AssessCompatibility(*loaded.document, runtime);
        std::cout << compatibility.status << '\n';
        if (!compatibility.compatible) return 1;
        std::cout << loaded.document->Sources().size() << " source(s), "
                  << loaded.document->Records().size() << " record(s)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "catalog compatibility probe failed: " << error.what() << '\n';
        return 2;
    }
}
