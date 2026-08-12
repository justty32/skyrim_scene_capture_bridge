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

}  // namespace CatalogFile
