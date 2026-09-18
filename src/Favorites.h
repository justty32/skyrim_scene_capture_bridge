#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

// Portable preference store, keyed only by durable base id.
class Favorites {
public:
    bool Contains(const std::string& id) const;
    void Add(const std::string& id);
    bool Toggle(const std::string& id);
    std::size_t Count() const;
    void LoadFrom(const std::filesystem::path& path);
    void SaveTo(const std::filesystem::path& path) const;
    std::string Serialize() const;
    bool Deserialize(const std::string& text);
    // order contains indices into ids; preserve that order within both groups.
    std::vector<std::size_t> OrderFavoritesFirst(const std::vector<std::string>& ids,
        std::vector<std::size_t> order) const;

private:
    std::unordered_set<std::string> ids_;
};
