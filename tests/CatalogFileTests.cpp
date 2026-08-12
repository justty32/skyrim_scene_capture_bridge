#include "CatalogFile.h"

#include <functional>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
    constexpr auto kValid = R"json({
  "schemaVersion": 1,
  "sources": [{
    "plugin": "CatalogOne.esp",
    "sourcePath": "C:/mods/CatalogOne.esp",
    "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "localized": false,
    "recordCount": 2,
    "loadOrderIndex": 0
  }],
  "records": [
    {
      "formKey": "CatalogOne.esp:0x000802",
      "plugin": "CatalogOne.esp",
      "recordType": "Static",
      "editorId": "MF_CatalogStatic",
      "name": null,
      "modelPath": "Architecture\\Farmhouse\\Farmhouse01.nif",
      "sourcePlugin": "CatalogOne.esp",
      "sourcePath": "C:/mods/CatalogOne.esp"
    },
    {
      "formKey": "CatalogOne.esp:0x000803",
      "plugin": "CatalogOne.esp",
      "recordType": "Npc",
      "editorId": null,
      "name": "Aldric",
      "modelPath": null,
      "sourcePlugin": "CatalogOne.esp",
      "sourcePath": "C:/mods/CatalogOne.esp"
    }
  ]
})json";

    void Check(bool condition, std::string_view message) {
        if (!condition) throw std::runtime_error(std::string(message));
    }

    CatalogFile::Document Parse(std::string text) {
        std::istringstream input(std::move(text));
        return CatalogFile::Document::Parse(input);
    }

    void ExpectError(std::string text, std::string_view fragment) {
        try {
            (void)Parse(std::move(text));
        } catch (const std::runtime_error& error) {
            Check(std::string_view(error.what()).find(fragment) != std::string_view::npos,
                "error did not identify the rejected field");
            return;
        }
        throw std::runtime_error("invalid catalog was accepted");
    }

    void ParsesAndIndexesRecords() {
        auto document = Parse(kValid);
        Check(document.Sources().size() == 1, "source count");
        Check(document.Records().size() == 2, "record count");
        const auto* record = document.Find("catalogone.ESP:0X000802");
        Check(record != nullptr, "case-insensitive durable FormKey lookup");
        Check(record->editorId == "MF_CatalogStatic", "editorId");
        Check(record->modelPath && record->modelPath->ends_with("Farmhouse01.nif"), "model path");
        Check(document.Find("CatalogOne.esp:0xFFFFFF") == nullptr, "missing lookup");
    }

    void EnrichmentPreservesRuntimeValues() {
        auto document = Parse(kValid);
        const auto* record = document.Find("CatalogOne.esp:0x000802");
        Check(record != nullptr, "metadata fixture lookup");
        CatalogFile::DisplayMetadata display{"", "Runtime Name", "Runtime\\Model.nif"};
        CatalogFile::Enrich(display, *record);
        Check(display.editorId == "MF_CatalogStatic", "offline EditorID fills missing runtime field");
        Check(display.name == "Runtime Name", "runtime name remains authoritative");
        Check(display.modelPath == "Runtime\\Model.nif", "runtime model remains authoritative");

        const auto* named = document.Find("CatalogOne.esp:0x000803");
        Check(named != nullptr, "named metadata fixture lookup");
        CatalogFile::DisplayMetadata missing;
        CatalogFile::Enrich(missing, *named);
        Check(missing.name == "Aldric", "offline name fills missing runtime field");
        Check(missing.modelPath.empty(), "offline null leaves missing runtime field empty");
    }

    void RejectsUnsupportedVersion() {
        auto json = std::string(kValid);
        json.replace(json.find("\"schemaVersion\": 1"), 18, "\"schemaVersion\": 2");
        ExpectError(std::move(json), "schemaVersion");
    }

    void AcceptsSchemaIntegerNumberTokens() {
        auto json = std::string(kValid);
        json.replace(json.find("\"schemaVersion\": 1"), 18, "\"schemaVersion\": 1.0");
        json.replace(json.find("\"recordCount\": 2"), 16, "\"recordCount\": 2.0");
        json.replace(json.find("\"loadOrderIndex\": 0"), 19, "\"loadOrderIndex\": 0.0");
        auto document = Parse(std::move(json));
        Check(document.Sources().front().recordCount == 2, "schema integer number token");
    }

    void RejectsMalformedFormKey() {
        auto json = std::string(kValid);
        json.replace(json.find("0x000802"), 8, "0x00080g");
        ExpectError(std::move(json), "records[0].formKey");
    }

    void RejectsIntegerOutsideUint64Range() {
        auto json = std::string(kValid);
        constexpr std::string_view original = "\"recordCount\": 2";
        json.replace(json.find(original), original.size(),
            "\"recordCount\": 18446744073709551616.0");
        ExpectError(std::move(json), "sources[0].recordCount");
    }

    void RejectsDuplicateFormKeysIgnoringCase() {
        auto json = std::string(kValid);
        constexpr std::string_view original = "CatalogOne.esp:0x000803";
        json.replace(json.find(original), original.size(), "catalogone.ESP:0x000802");
        ExpectError(std::move(json), "duplicate durable FormKey");
    }

    void RejectsContractDrift() {
        auto json = std::string(kValid);
        const auto marker = json.find("\"recordType\": \"Static\"");
        json.insert(marker, "\"unexpected\": true,\n      ");
        ExpectError(std::move(json), "unexpected property");
    }

    void FileLoadingFailsSoft() {
        const auto path = std::filesystem::temp_directory_path() /
            "scene-capture-bridge-catalog-file-tests.json";
        std::filesystem::remove(path);
        const auto missing = CatalogFile::TryLoad(path);
        Check(!missing.document && missing.status.find("not found") != std::string::npos,
            "missing file status");

        {
            std::ofstream output(path);
            output << "{ broken";
        }
        const auto malformed = CatalogFile::TryLoad(path);
        std::filesystem::remove(path);
        Check(!malformed.document && malformed.status.find("rejected") != std::string::npos,
            "malformed file status");
    }

    void CompatibilityRequiresExactRuntimeSourceSet() {
        auto document = Parse(kValid);
        auto assessment = CatalogFile::AssessCompatibility(document, {{"catalogone.ESP"}});
        Check(assessment.compatible, "case-insensitive exact source set");
        assessment = CatalogFile::AssessCompatibility(document, {});
        Check(!assessment.compatible && assessment.status.find("not loaded") != std::string::npos,
            "missing runtime plugin");
        assessment = CatalogFile::AssessCompatibility(document,
            {{"CatalogOne.esp"}, {"Extra.esp"}});
        Check(!assessment.compatible && assessment.status.find("absent from catalog") != std::string::npos,
            "extra runtime plugin");
    }

    void CompatibilityRejectsBrokenProvenance() {
        auto duplicate = std::string(kValid);
        const auto sourceEnd = duplicate.find("  }],\n  \"records\"");
        const auto sourceStart = duplicate.find("{\n    \"plugin\"");
        const auto source = duplicate.substr(sourceStart, sourceEnd - sourceStart + 3);
        duplicate.insert(sourceEnd + 3, ",\n" + source);
        auto duplicated = Parse(std::move(duplicate));
        auto assessment = CatalogFile::AssessCompatibility(duplicated,
            {{"CatalogOne.esp"}});
        Check(!assessment.compatible && assessment.status.find("duplicate source") != std::string::npos,
            "duplicate catalog source");

        auto unknown = std::string(kValid);
        constexpr std::string_view original = "\"sourcePlugin\": \"CatalogOne.esp\"";
        unknown.replace(unknown.find(original), original.size(),
            "\"sourcePlugin\": \"Missing.esp\"");
        auto unknownSource = Parse(std::move(unknown));
        assessment = CatalogFile::AssessCompatibility(unknownSource,
            {{"CatalogOne.esp"}});
        Check(!assessment.compatible && assessment.status.find("absent from sources") != std::string::npos,
            "record sourcePlugin cross-reference");

        auto gap = std::string(kValid);
        const auto sourceEnd2 = gap.find("  }],\n  \"records\"");
        const auto sourceStart2 = gap.find("{\n    \"plugin\"");
        auto second = gap.substr(sourceStart2, sourceEnd2 - sourceStart2 + 3);
        second.replace(second.find("CatalogOne.esp"), 14, "CatalogTwo.esp");
        second.replace(second.find("\"loadOrderIndex\": 0"), 19, "\"loadOrderIndex\": 2");
        gap.insert(sourceEnd2 + 3, ",\n" + second);
        auto gapped = Parse(std::move(gap));
        assessment = CatalogFile::AssessCompatibility(gapped,
            {{"CatalogOne.esp"}, {"CatalogTwo.esp"}});
        Check(!assessment.compatible && assessment.status.find("not unique and contiguous") !=
            std::string::npos, "gapped catalog source order");
    }

    void CompatibilityChecksGlobalRuntimeOrder() {
        auto json = std::string(kValid);
        const auto sourceEnd = json.find("  }],\n  \"records\"");
        const auto sourceStart = json.find("{\n    \"plugin\"");
        auto second = json.substr(sourceStart, sourceEnd - sourceStart + 3);
        second.replace(second.find("CatalogOne.esp"), 14, "CatalogTwo.esp");
        second.replace(second.find("\"loadOrderIndex\": 0"), 19, "\"loadOrderIndex\": 1");
        json.insert(sourceEnd + 3, ",\n" + second);
        auto document = Parse(std::move(json));

        auto assessment = CatalogFile::AssessCompatibility(document,
            {{"CatalogTwo.esp"}, {"CatalogOne.esp"}});
        Check(!assessment.compatible && assessment.status.find("order differs") != std::string::npos,
            "reversed runtime order");
        assessment = CatalogFile::AssessCompatibility(document,
            {{"CatalogOne.esp"}, {"CatalogTwo.esp"}});
        Check(assessment.compatible, "matching global runtime order");
    }

    void CompatibilityRejectsFormKeyOriginMismatch() {
        auto json = std::string(kValid);
        constexpr std::string_view original = "\"plugin\": \"CatalogOne.esp\"";
        const auto record = json.find(original, json.find("\"records\""));
        json.replace(record, original.size(), "\"plugin\": \"Other.esp\"");
        auto document = Parse(std::move(json));
        auto assessment = CatalogFile::AssessCompatibility(document,
            {{"CatalogOne.esp"}});
        Check(!assessment.compatible && assessment.status.find("origin disagrees") !=
            std::string::npos, "FormKey plugin prefix cross-reference");
    }
}

int main() {
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"parses and indexes records", ParsesAndIndexesRecords},
        {"enrichment preserves runtime values", EnrichmentPreservesRuntimeValues},
        {"rejects unsupported version", RejectsUnsupportedVersion},
        {"accepts schema integer number tokens", AcceptsSchemaIntegerNumberTokens},
        {"rejects malformed FormKey", RejectsMalformedFormKey},
        {"rejects integer outside uint64 range", RejectsIntegerOutsideUint64Range},
        {"rejects duplicate FormKeys", RejectsDuplicateFormKeysIgnoringCase},
        {"rejects contract drift", RejectsContractDrift},
        {"file loading fails soft", FileLoadingFailsSoft},
        {"compatibility requires exact runtime sources", CompatibilityRequiresExactRuntimeSourceSet},
        {"compatibility rejects broken provenance", CompatibilityRejectsBrokenProvenance},
        {"compatibility checks global runtime order", CompatibilityChecksGlobalRuntimeOrder},
        {"compatibility rejects FormKey origin mismatch", CompatibilityRejectsFormKeyOriginMismatch},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
