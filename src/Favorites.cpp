#include "Favorites.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>

bool Favorites::Contains(const std::string& id) const { return ids_.contains(id); }
void Favorites::Add(const std::string& id) { ids_.insert(id); }
bool Favorites::Toggle(const std::string& id) {
    if (ids_.erase(id)) return false;
    Add(id);
    return true;
}
std::size_t Favorites::Count() const { return ids_.size(); }

std::string Favorites::Serialize() const {
    std::vector<std::string> ids(ids_.begin(), ids_.end());
    std::sort(ids.begin(), ids.end());
    return nlohmann::json{{"contract", "scene-capture-favorites/1"}, {"ids", ids}}.dump(2);
}

bool Favorites::Deserialize(const std::string& text) {
    ids_.clear();
    try {
        const auto root = nlohmann::json::parse(text);
        if (root.at("contract") != "scene-capture-favorites/1" || !root.at("ids").is_array()) {
            std::clog << "Favorites: unsupported contract or invalid ids; using empty set\n";
            return false;
        }
        std::unordered_set<std::string> parsed;
        for (const auto& id : root.at("ids")) parsed.insert(id.get<std::string>());
        ids_ = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        std::clog << "Favorites: invalid JSON; using empty set\n";
        return false;
    }
}

void Favorites::LoadFrom(const std::filesystem::path& path) {
    ids_.clear();
    std::ifstream input(path);
    if (!input) {
        std::clog << "Favorites: file missing or unreadable; using empty set\n";
        return;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        std::clog << "Favorites: read failed; using empty set\n";
        return;
    }
    Deserialize(text);
}

void Favorites::SaveTo(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::trunc);
    if (output) {
        output << Serialize() << '\n';
        output.flush();
    }
    if (!output) std::clog << "Favorites: write failed\n";
}

std::vector<std::size_t> Favorites::OrderFavoritesFirst(const std::vector<std::string>& ids,
    std::vector<std::size_t> order) const {
    std::stable_partition(order.begin(), order.end(),
        [&](std::size_t index) { return Contains(ids[index]); });
    return order;
}
