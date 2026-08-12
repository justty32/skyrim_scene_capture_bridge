#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CatalogFile {

    struct Source {
        std::string plugin;
        std::string sourcePath;
        std::string sha256;
        bool localized = false;
        std::uint64_t recordCount = 0;
        std::uint64_t loadOrderIndex = 0;
    };

    struct Record {
        std::string formKey;
        std::string plugin;
        std::string recordType;
        std::optional<std::string> editorId;
        std::optional<std::string> name;
        std::optional<std::string> modelPath;
        std::string sourcePlugin;
        std::string sourcePath;
    };

    struct DisplayMetadata {
        std::string editorId;
        std::string name;
        std::string modelPath;
    };

    // Runtime data is authoritative. Offline values only fill fields Skyrim did
    // not retain in memory (normally editorId, and names for many statics).
    void Enrich(DisplayMetadata& target, const Record& source);

    class Document {
    public:
        [[nodiscard]] static Document Parse(std::istream& input);
        [[nodiscard]] static Document Load(const std::filesystem::path& path);

        [[nodiscard]] const std::vector<Source>& Sources() const { return sources_; }
        [[nodiscard]] const std::vector<Record>& Records() const { return records_; }
        [[nodiscard]] const Record* Find(std::string_view formKey) const;

    private:
        std::vector<Source> sources_;
        std::vector<Record> records_;
        std::unordered_map<std::string, std::size_t> byFormKey_;
    };

    struct LoadResult {
        std::optional<Document> document;
        std::string status;
    };

    // Fail-soft boundary used by the SKSE runtime and portable tests. Missing,
    // inaccessible, or invalid files are reported without throwing.
    [[nodiscard]] LoadResult TryLoad(const std::filesystem::path& path);

    struct Compatibility {
        bool compatible = false;
        std::string status;
    };

    struct RuntimeSource {
        std::string plugin;
    };

    // Structural provenance + exact runtime source/global-order gate. Names are
    // compared case-insensitively because Windows plugin identity is case-insensitive.
    // File digests are deliberately outside this pure core.
    [[nodiscard]] Compatibility AssessCompatibility(const Document& document,
        const std::vector<RuntimeSource>& runtimePlugins);

}  // namespace CatalogFile
