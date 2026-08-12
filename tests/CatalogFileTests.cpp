#include "CatalogFile.h"

#include <functional>
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
}

int main() {
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"parses and indexes records", ParsesAndIndexesRecords},
        {"rejects unsupported version", RejectsUnsupportedVersion},
        {"accepts schema integer number tokens", AcceptsSchemaIntegerNumberTokens},
        {"rejects malformed FormKey", RejectsMalformedFormKey},
        {"rejects integer outside uint64 range", RejectsIntegerOutsideUint64Range},
        {"rejects duplicate FormKeys", RejectsDuplicateFormKeysIgnoringCase},
        {"rejects contract drift", RejectsContractDrift},
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
