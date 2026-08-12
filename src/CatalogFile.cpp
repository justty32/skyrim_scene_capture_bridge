#include "CatalogFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace {
    using Json = nlohmann::json;

    std::runtime_error Error(std::string_view path, std::string_view message) {
        return std::runtime_error("scene catalog " + std::string(path) + ": " + std::string(message));
    }

    void RequireKeys(const Json& value, std::string_view path,
        std::initializer_list<std::string_view> keys) {
        if (!value.is_object()) throw Error(path, "expected an object");
        std::unordered_set<std::string_view> allowed(keys);
        for (const auto& [key, unused] : value.items()) {
            if (!allowed.contains(key)) throw Error(path, "unexpected property '" + key + "'");
        }
        for (const auto key : keys) {
            if (!value.contains(key)) throw Error(path, "missing property '" + std::string(key) + "'");
        }
    }

    const std::string& String(const Json& object, std::string_view key, std::string_view path) {
        const auto& value = object.at(key);
        if (!value.is_string() || value.get_ref<const std::string&>().empty())
            throw Error(path, "expected a non-empty string");
        return value.get_ref<const std::string&>();
    }

    std::optional<std::string> NullableString(const Json& object,
        std::string_view key, std::string_view path) {
        const auto& value = object.at(key);
        if (value.is_null()) return std::nullopt;
        if (!value.is_string()) throw Error(path, "expected a string or null");
        return value.get<std::string>();
    }

    std::uint64_t Unsigned(const Json& value, std::string_view path) {
        if (value.is_number_unsigned()) return value.get<std::uint64_t>();
        if (value.is_number_integer()) {
            const auto number = value.get<std::int64_t>();
            if (number >= 0) return static_cast<std::uint64_t>(number);
        } else if (value.is_number_float()) {
            const auto number = static_cast<long double>(value.get<double>());
            if (std::isfinite(number) && number >= 0 && std::trunc(number) == number &&
                number < std::ldexp(1.0L, 64))
                return static_cast<std::uint64_t>(number);
        }
        throw Error(path, "expected a non-negative integer within uint64 range");
    }

    std::string Lower(std::string_view value) {
        std::string out(value);
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool ValidFormKey(std::string_view value) {
        const auto marker = value.rfind(":0x");
        if (marker == std::string_view::npos || marker + 9 != value.size()) return false;
        const auto plugin = Lower(value.substr(0, marker));
        const bool extension = plugin.ends_with(".esm") || plugin.ends_with(".esp") ||
            plugin.ends_with(".esl");
        if (!extension || marker <= 4) return false;
        return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(marker + 3), value.end(),
            [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); });
    }

    CatalogFile::Source ParseSource(const Json& value, std::size_t index) {
        const auto path = "sources[" + std::to_string(index) + "]";
        RequireKeys(value, path, {"plugin", "sourcePath", "sha256", "localized",
            "recordCount", "loadOrderIndex"});
        CatalogFile::Source source;
        source.plugin = String(value, "plugin", path + ".plugin");
        source.sourcePath = String(value, "sourcePath", path + ".sourcePath");
        source.sha256 = String(value, "sha256", path + ".sha256");
        if (source.sha256.size() != 64 || !std::all_of(source.sha256.begin(), source.sha256.end(),
            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            throw Error(path + ".sha256", "expected 64 lowercase hexadecimal characters");
        if (!value.at("localized").is_boolean())
            throw Error(path + ".localized", "expected a boolean");
        source.localized = value.at("localized").get<bool>();
        source.recordCount = Unsigned(value.at("recordCount"), path + ".recordCount");
        source.loadOrderIndex = Unsigned(value.at("loadOrderIndex"), path + ".loadOrderIndex");
        return source;
    }

    CatalogFile::Record ParseRecord(const Json& value, std::size_t index) {
        const auto path = "records[" + std::to_string(index) + "]";
        RequireKeys(value, path, {"formKey", "plugin", "recordType", "editorId", "name",
            "modelPath", "sourcePlugin", "sourcePath"});
        CatalogFile::Record record;
        record.formKey = String(value, "formKey", path + ".formKey");
        if (!ValidFormKey(record.formKey))
            throw Error(path + ".formKey", "expected <plugin>.es[m|p|l]:0x followed by 6 uppercase hex digits");
        record.plugin = String(value, "plugin", path + ".plugin");
        record.recordType = String(value, "recordType", path + ".recordType");
        record.editorId = NullableString(value, "editorId", path + ".editorId");
        record.name = NullableString(value, "name", path + ".name");
        record.modelPath = NullableString(value, "modelPath", path + ".modelPath");
        record.sourcePlugin = String(value, "sourcePlugin", path + ".sourcePlugin");
        record.sourcePath = String(value, "sourcePath", path + ".sourcePath");
        return record;
    }
}

namespace CatalogFile {

    void Enrich(DisplayMetadata& target, const Record& source) {
        if (target.editorId.empty() && source.editorId) target.editorId = *source.editorId;
        if (target.name.empty() && source.name) target.name = *source.name;
        if (target.modelPath.empty() && source.modelPath) target.modelPath = *source.modelPath;
    }

    Document Document::Parse(std::istream& input) {
        Json root;
        try {
            input >> root;
        } catch (const Json::exception& error) {
            throw Error("JSON", error.what());
        }
        RequireKeys(root, "$", {"schemaVersion", "sources", "records"});
        if (Unsigned(root.at("schemaVersion"), "schemaVersion") != 1)
            throw Error("schemaVersion", "expected supported version 1");
        if (!root.at("sources").is_array()) throw Error("sources", "expected an array");
        if (!root.at("records").is_array()) throw Error("records", "expected an array");

        Document document;
        for (std::size_t i = 0; i < root.at("sources").size(); ++i)
            document.sources_.push_back(ParseSource(root.at("sources")[i], i));
        for (std::size_t i = 0; i < root.at("records").size(); ++i) {
            auto record = ParseRecord(root.at("records")[i], i);
            const auto normalized = Lower(record.formKey);
            if (document.byFormKey_.contains(normalized))
                throw Error("records[" + std::to_string(i) + "].formKey",
                    "duplicate durable FormKey '" + record.formKey + "'");
            document.byFormKey_.emplace(normalized, document.records_.size());
            document.records_.push_back(std::move(record));
        }
        return document;
    }

    Document Document::Load(const std::filesystem::path& path) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("cannot open scene catalog: " + path.string());
        return Parse(input);
    }

    const Record* Document::Find(std::string_view formKey) const {
        const auto found = byFormKey_.find(Lower(formKey));
        return found == byFormKey_.end() ? nullptr : &records_[found->second];
    }

    LoadResult TryLoad(const std::filesystem::path& path) {
        std::error_code filesystemError;
        const bool exists = std::filesystem::exists(path, filesystemError);
        if (filesystemError)
            return {std::nullopt, "scene-catalog.json inaccessible: " + filesystemError.message()};
        if (!exists)
            return {std::nullopt, "scene-catalog.json not found (runtime metadata only)"};
        try {
            auto document = Document::Load(path);
            const auto count = document.Records().size();
            return {std::move(document), "loaded scene-catalog.json (" + std::to_string(count) +
                " offline record(s))"};
        } catch (const std::exception& error) {
            return {std::nullopt, "scene-catalog.json rejected: " + std::string(error.what())};
        }
    }

    Compatibility AssessCompatibility(const Document& document,
        const std::vector<RuntimeSource>& runtimePlugins) {
        std::unordered_set<std::string> sources;
        std::vector<bool> seenIndices(document.Sources().size(), false);
        std::vector<std::string> orderedSources(document.Sources().size());
        for (const auto& source : document.Sources()) {
            const auto plugin = Lower(source.plugin);
            if (!sources.insert(plugin).second)
                return {false, "catalog has duplicate source plugin '" + source.plugin + "'"};
            if (source.loadOrderIndex >= seenIndices.size() || seenIndices[source.loadOrderIndex])
                return {false, "catalog source loadOrderIndex values are not unique and contiguous"};
            seenIndices[source.loadOrderIndex] = true;
            orderedSources[source.loadOrderIndex] = plugin;
        }
        for (const auto& record : document.Records()) {
            const auto marker = record.formKey.rfind(":0x");
            const auto formPlugin = Lower(record.formKey.substr(0, marker));
            if (formPlugin != Lower(record.plugin))
                return {false, "catalog record FormKey origin disagrees with plugin: " +
                    record.formKey};
            if (!sources.contains(Lower(record.plugin)))
                return {false, "catalog record origin plugin is absent from sources: " +
                    record.plugin};
            if (!sources.contains(Lower(record.sourcePlugin)))
                return {false, "catalog record sourcePlugin is absent from sources: " +
                    record.sourcePlugin};
        }

        std::unordered_set<std::string> runtime;
        for (const auto& source : runtimePlugins) {
            if (!runtime.insert(Lower(source.plugin)).second)
                return {false, "runtime reports duplicate plugin '" + source.plugin + "'"};
        }
        for (const auto& plugin : sources)
            if (!runtime.contains(plugin))
                return {false, "catalog source is not loaded: " + plugin};
        for (const auto& plugin : runtime)
            if (!sources.contains(plugin))
                return {false, "loaded plugin is absent from catalog: " + plugin};
        for (std::size_t i = 0; i < orderedSources.size(); ++i) {
            if (orderedSources[i] != Lower(runtimePlugins[i].plugin))
                return {false, "catalog source order differs from runtime order"};
        }
        return {true, "source names/global order matched runtime; source hashes not runtime-verified"};
    }

}  // namespace CatalogFile
