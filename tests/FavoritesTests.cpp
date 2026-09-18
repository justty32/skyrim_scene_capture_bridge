#include "Favorites.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
    const std::string kA = "Skyrim.esm:0x000001";
    const std::string kB = "Skyrim.esm:0x000002";
    const std::string kC = "Skyrim.esm:0x000003";
    const std::string kD = "Skyrim.esm:0x000004";

    void Check(bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    }

    void ToggleTwice() {
        Favorites favorites;
        Check(!favorites.Contains(kA) && favorites.Count() == 0, "initial state");
        Check(favorites.Toggle(kA), "toggle on");
        Check(favorites.Contains(kA) && favorites.Count() == 1, "count increases");
        Check(!favorites.Toggle(kA), "toggle off");
        Check(!favorites.Contains(kA) && favorites.Count() == 0, "original state restored");
    }

    void DuplicateAdd() {
        Favorites favorites;
        favorites.Add(kA);
        favorites.Add(kA);
        Check(favorites.Contains(kA) && favorites.Count() == 1, "duplicate Add is idempotent");
    }

    void SortedSerialization() {
        Favorites favorites;
        favorites.Add(kC);
        favorites.Add(kA);
        favorites.Add(kB);
        const auto root = nlohmann::json::parse(favorites.Serialize());
        Check(root.at("ids") == nlohmann::json::array({kA, kB, kC}), "serialized ids sorted");
    }

    void RoundTrip() {
        Favorites original;
        original.Add(kB);
        original.Add(kA);
        Favorites restored;
        Check(restored.Deserialize(original.Serialize()), "deserialize succeeds");
        Check(restored.Count() == original.Count() && restored.Contains(kA) &&
            restored.Contains(kB), "round-trip preserves set");
    }

    void BadJsonFailsOpen() {
        Favorites favorites;
        favorites.Add(kA);
        Check(!favorites.Deserialize("{broken"), "bad JSON rejected without throwing");
        Check(favorites.Count() == 0, "bad JSON clears existing set");
    }

    void UnknownContractFailsOpen() {
        Favorites favorites;
        favorites.Add(kA);
        Check(!favorites.Deserialize(
            R"({"contract":"scene-capture-favorites/2","ids":["Skyrim.esm:0x000002"]})"),
            "unknown contract rejected without throwing");
        Check(favorites.Count() == 0, "unknown contract clears existing set");
    }

    void StableFavoritesFirst() {
        Favorites favorites;
        favorites.Add(kA);
        favorites.Add(kC);
        const std::vector<std::string> ids{kA, kB, kC, kD};
        const std::vector<std::size_t> order{3, 2, 1, 0};
        Check(favorites.OrderFavoritesFirst(ids, order) ==
            std::vector<std::size_t>{2, 0, 3, 1}, "both groups preserve input order");
    }

    void NoFavoritesIdentity() {
        Favorites favorites;
        const std::vector<std::string> ids{kA, kB, kC, kD};
        const std::vector<std::size_t> order{3, 1, 0, 2};
        Check(favorites.OrderFavoritesFirst(ids, order) == order, "empty favorites preserves order");
    }
}

int main() {
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"toggle twice and count", ToggleTwice},
        {"duplicate Add", DuplicateAdd},
        {"sorted serialization", SortedSerialization},
        {"round-trip", RoundTrip},
        {"bad JSON fails open", BadJsonFailsOpen},
        {"unknown contract fails open", UnknownContractFailsOpen},
        {"stable favorites first", StableFavoritesFirst},
        {"no favorites identity", NoFavoritesIdentity},
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
