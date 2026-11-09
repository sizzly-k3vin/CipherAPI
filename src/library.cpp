#include "library.hpp"
#include "logger.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

LibraryManager::LibraryManager(const std::string& directory) {
    load(directory);
}

void LibraryManager::load(const std::string& directory) {
    for (std::size_t i = 0; i < COUNT; ++i) {
        const std::string name =
            "lib" + std::string(i + 1 < 10 ? "0" : "") +
            std::to_string(i + 1) + ".txt";

        std::ifstream file(fs::path(directory) / name);

        if (!file)
            throw std::runtime_error("Cannot open " + name);

        std::string line;
        std::size_t entries = 0;

        while (std::getline(file, line)) {
            auto separator = line.find(" = ");

            if (separator != 1 || line.size() - separator - 3 != 64)
                throw std::runtime_error("Invalid entry in " + name);

            auto character = static_cast<unsigned char>(line[0]);
            std::string token = line.substr(separator + 3);

            for (char c : token) {
                if (!((c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f')))
                    throw std::runtime_error("Invalid token in " + name);
            }

            if (!forward_[i].emplace(character, token).second)
                throw std::runtime_error("Duplicate character in " + name);

            if (!reverse_.emplace(
                    token, TokenEntry{character, i}).second)
                throw std::runtime_error("Duplicate token detected");

            ++entries;
        }

        if (entries != 95)
            throw std::runtime_error("Incomplete library: " + name);

        CipherLog::info(name + " loaded: " +
                        std::to_string(entries) + " entries");
    }

    CipherLog::info("All 10 libraries loaded.");
}

std::string LibraryManager::encode(
    const std::string& message) const {

    std::array<std::size_t, 256> counters{};
    std::string output;
    output.reserve(message.size() * 64);

    for (unsigned char ch : message) {
        if (ch < 32 || ch > 126)
            throw std::invalid_argument("Printable ASCII only for now");

        std::size_t lib = counters[ch] % COUNT;
        output += forward_[lib].at(ch);
        ++counters[ch];
    }

    CipherLog::info("Encoded " + std::to_string(message.size()) +
                    " characters into " +
                    std::to_string(output.size()) + " token characters.");

    return output;
}

std::string LibraryManager::decode(
    const std::string& stream) const {

    if (stream.size() % 64 != 0)
        throw std::invalid_argument("Invalid stream length");

    std::array<std::size_t, 256> counters{};
    std::string output;
    output.reserve(stream.size() / 64);

    for (std::size_t pos = 0; pos < stream.size(); pos += 64) {
        std::string token = stream.substr(pos, 64);
        auto found = reverse_.find(token);

        if (found == reverse_.end())
            throw std::runtime_error("Unknown token");

        const auto& entry = found->second;
        std::size_t expected = counters[entry.character] % COUNT;

        if (entry.library != expected)
            throw std::runtime_error("Library rotation mismatch");

        output += static_cast<char>(entry.character);
        ++counters[entry.character];
    }

    CipherLog::info("Decoded " + std::to_string(output.size()) +
                    " characters successfully.");

    return output;
}
