#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>

class LibraryManager {
public:
    explicit LibraryManager(const std::string& directory);
    std::string encode(const std::string& message) const;
    std::string decode(const std::string& stream) const;

private:
    static constexpr std::size_t COUNT = 10;

    struct TokenEntry {
        unsigned char character;
        std::size_t library;
    };

    std::array<std::unordered_map<unsigned char, std::string>,
               COUNT> forward_;

    std::unordered_map<std::string, TokenEntry> reverse_;

    void load(const std::string& directory);
};
