#include "debug.hpp"
#include "library.hpp"
#include "logger.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

LibraryManager::LibraryManager(const std::string& directory) {
    load(directory);
}

void LibraryManager::load(const std::string& directory) {
    for (std::size_t i = 0; i < COUNT; ++i) {
        const std::string name =
            "lib" + std::string(i + 1 < 10 ? "0" : "") +
            std::to_string(i + 1) + ".txt";

        const fs::path path = fs::path(directory) / name;

        auto fail = [&](const std::string& code,
                        const std::string& reason) {
            const std::string message =
                "[" + code + "] " + name + ": " + reason;

            CipherLog::error(message);
            throw std::runtime_error(message);
        };

        // Check whether the library exists.
        std::error_code fs_error;
        bool exists = fs::exists(path, fs_error);

        if (fs_error)
            fail("LIB-003", "Filesystem error: " + fs_error.message());

        if (!exists)
            fail("LIB-001", "File not found: " + path.string());

        // Open library.
        errno = 0;
        std::ifstream file(path);

        if (!file) {
            const int err = errno;

            if (err == EACCES)
                fail("LIB-002", "Permission denied");

            if (err == ENOENT)
                fail("LIB-001", "File disappeared before opening");

            fail("LIB-003", "Failed to open file (errno=" +
                            std::to_string(err) + ")");
        }

        std::string line;
        std::size_t entries = 0;
        std::size_t line_number = 0;

        while (std::getline(file, line)) {
            ++line_number;

            // Expected format: X = 64-character hexadecimal token
            auto separator = line.find(" = ");

            if (separator != 1 || line.size() != 68) {
                fail("LIB-004",
                     "Invalid entry format at line " +
                     std::to_string(line_number));
            }

            auto character = static_cast<unsigned char>(line[0]);

            if (character < 32 || character > 126) {
                fail("LIB-004",
                     "Character outside printable ASCII at line " +
                     std::to_string(line_number));
            }

            std::string token = line.substr(separator + 3);

            for (char c : token) {
                if (!((c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f'))) {
                    fail("LIB-004",
                         "Invalid hexadecimal token at line " +
                         std::to_string(line_number));
                }
            }

            if (forward_[i].count(character)) {
                fail("LIB-005",
                     "Duplicate character at line " +
                     std::to_string(line_number));
            }

            if (reverse_.count(token)) {
                fail("LIB-008",
                     "Duplicate token at line " +
                     std::to_string(line_number));
            }

            forward_[i].emplace(character, token);
            reverse_.emplace(token, TokenEntry{character, i});

            ++entries;
        }

        // Detect file read errors.
        if (file.bad() || (!file.eof() && file.fail())) {
            fail("LIB-007", "I/O error while reading file");
        }

        // Every library must contain exactly 95 entries.
        if (entries != 95) {
            fail("LIB-006",
                 "Expected 95 entries, found " +
                 std::to_string(entries));
        }

        CipherLog::info(
            "[LIB] " + name + " loaded: " +
            std::to_string(entries) + " entries"
        );
    }

    CipherLog::info("[LIB] All 10 libraries loaded successfully.");
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

    CipherLog::info(
        "Encoded " + std::to_string(message.size()) +
        " characters into " +
        std::to_string(output.size()) + " token characters."
    );

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

    CipherLog::info(
        "Decoded " + std::to_string(output.size()) +
        " characters successfully."
    );

    return output;
}
