#include "CatalogFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
    void Check(bool condition, std::string_view message) {
        if (!condition) throw std::runtime_error(std::string(message));
    }

    std::string ReadAll(const std::filesystem::path& path) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("cannot read producer output: " + path.string());
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    CatalogFile::Document Parse(std::string text) {
        std::istringstream input(std::move(text));
        return CatalogFile::Document::Parse(input);
    }

    void ExpectParseError(std::string text, std::string_view fragment) {
        try {
            (void)Parse(std::move(text));
        } catch (const std::runtime_error& error) {
            Check(std::string_view(error.what()).find(fragment) != std::string_view::npos,
                "parser rejected producer drift for the wrong reason");
            return;
        }
        throw std::runtime_error("drifted ModForge output was accepted");
    }

    bool SamePath(const std::string& actual, const std::filesystem::path& expected) {
        std::error_code error;
        const bool same = std::filesystem::equivalent(actual, expected, error);
        return !error && same;
    }

    bool IsLowerHexDigest(std::string_view digest) {
        return digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
    }
}

int main(int argc, char** argv) {
    try {
        Check(argc == 4,
            "usage: modforge_catalog_contract_tests <scene-catalog.json> <full.esp> <light.esl>");
        const std::filesystem::path catalogPath = argv[1];
        const std::filesystem::path fullPath = argv[2];
        const std::filesystem::path lightPath = argv[3];

        const auto loaded = CatalogFile::TryLoad(catalogPath);
        Check(loaded.document.has_value(), "real ModForge scene-catalog.json did not parse");
        const auto& document = *loaded.document;
        Check(document.Sources().size() == 2, "producer source count drifted");
        Check(document.Sources()[0].plugin == "ContractFull.esp", "full plugin order/provenance");
        Check(document.Sources()[0].loadOrderIndex == 0, "full plugin global order index");
        Check(document.Sources()[1].plugin == "ContractLight.esl", "light plugin order/provenance");
        Check(document.Sources()[1].loadOrderIndex == 1, "light plugin global order index");
        Check(SamePath(document.Sources()[0].sourcePath, fullPath), "full plugin sourcePath");
        Check(SamePath(document.Sources()[1].sourcePath, lightPath), "light plugin sourcePath");
        Check(IsLowerHexDigest(document.Sources()[0].sha256), "full plugin SHA-256 shape");
        Check(IsLowerHexDigest(document.Sources()[1].sha256), "light plugin SHA-256 shape");
        Check(document.Sources()[0].recordCount > 0 && document.Sources()[1].recordCount > 0,
            "producer record-count provenance");

        const auto* full = document.Find("contractfull.ESP:0X000800");
        Check(full != nullptr, "full-plugin durable FormKey lookup");
        Check(full->plugin == "ContractFull.esp", "full record origin provenance");
        Check(full->sourcePlugin == "ContractFull.esp", "full record winner provenance");
        Check(SamePath(full->sourcePath, fullPath), "full record sourcePath provenance");
        Check(full->recordType == "MiscItem", "full record type");
        Check(full->editorId && *full->editorId == "MF_ContractFull", "full EditorID metadata");
        Check(full->name && *full->name == "Full catalog metadata", "full name metadata");
        Check(full->modelPath && *full->modelPath == "Clutter\\ContractFull.nif",
            "full model metadata");

        CatalogFile::DisplayMetadata missing;
        CatalogFile::Enrich(missing, *full);
        Check(missing.editorId == "MF_ContractFull", "offline EditorID merge");
        Check(missing.name == "Full catalog metadata", "offline name merge");
        Check(missing.modelPath == "Clutter\\ContractFull.nif", "offline model merge");
        CatalogFile::DisplayMetadata runtime{"RuntimeEditorId", "Runtime Name", "Runtime\\Model.nif"};
        CatalogFile::Enrich(runtime, *full);
        Check(runtime.editorId == "RuntimeEditorId" && runtime.name == "Runtime Name" &&
            runtime.modelPath == "Runtime\\Model.nif", "runtime metadata remains authoritative");

        const auto* light = document.Find("ContractLight.esl:0x000800");
        Check(light != nullptr, "light-plugin durable FormKey lookup");
        Check(light->editorId && *light->editorId == "MF_ContractLight", "light EditorID metadata");
        Check(light->name && *light->name == "Light catalog metadata", "light name metadata");
        Check(light->sourcePlugin == "ContractLight.esl" && SamePath(light->sourcePath, lightPath),
            "light record provenance");

        auto compatibility = CatalogFile::AssessCompatibility(document,
            {{"ContractFull.esp"}, {"ContractLight.esl"}});
        Check(compatibility.compatible, "matching full/light global source order");
        compatibility = CatalogFile::AssessCompatibility(document, {{"ContractFull.esp"}});
        Check(!compatibility.compatible && compatibility.status.find("not loaded") != std::string::npos,
            "missing runtime source must fail closed");
        compatibility = CatalogFile::AssessCompatibility(document,
            {{"ContractFull.esp"}, {"ContractLight.esl"}, {"Extra.esp"}});
        Check(!compatibility.compatible && compatibility.status.find("absent from catalog") != std::string::npos,
            "extra runtime source must fail closed");
        compatibility = CatalogFile::AssessCompatibility(document,
            {{"ContractLight.esl"}, {"ContractFull.esp"}});
        Check(!compatibility.compatible && compatibility.status.find("order differs") != std::string::npos,
            "reversed full/light global order must fail closed");

        auto producerJson = ReadAll(catalogPath);
        auto drifted = producerJson;
        const auto objectStart = drifted.find('{');
        Check(objectStart != std::string::npos, "producer JSON root");
        drifted.insert(objectStart + 1, "\n  \"unexpectedProducerField\": true,");
        ExpectParseError(std::move(drifted), "unexpected property");

        auto malformed = producerJson;
        malformed.pop_back();
        while (!malformed.empty() && (malformed.back() == '\n' || malformed.back() == '\r'))
            malformed.pop_back();
        malformed.pop_back();
        ExpectParseError(std::move(malformed), "JSON");

        std::cout << "PASS: live ModForge catalog export -> CatalogFile contract\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: live ModForge catalog contract: " << error.what() << '\n';
        return 1;
    }
}
