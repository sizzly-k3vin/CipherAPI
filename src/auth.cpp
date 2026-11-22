#include "auth.hpp"

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <utility>

AuthStore::AuthStore(const std::string& file) : path(file) {
    if (!std::filesystem::exists(path))
        return;

    std::ifstream input(path);
    if (!input.is_open())
        throw std::runtime_error("Cannot read user database");

    const auto is_hex = [](const std::string& value) {
        for (unsigned char c : value) {
            if (!std::isxdigit(c))
                return false;
        }
        return true;
    };

    std::unordered_map<std::string, UserRecord> loaded;
    std::string username;
    bool needs_migration = false;

    while (input >> username) {
        std::string first;
        if (!(input >> first))
            throw std::runtime_error("Malformed user database record");

        UserRecord record;

        if (first.size() == 64) {
            record.user_id = generate_user_id();
            record.ckey_hash = first;
            needs_migration = true;
        } else {
            record.user_id = first;
            if (!(input >> record.ckey_hash))
                throw std::runtime_error("Malformed user database record");
        }

        if (!valid_username(username) ||
            record.user_id.size() != 32 ||
            record.ckey_hash.size() != 64 ||
            !is_hex(record.user_id) ||
            !is_hex(record.ckey_hash)) {
            throw std::runtime_error("Invalid user database record");
        }

        if (!loaded.emplace(username, record).second)
            throw std::runtime_error("Duplicate user in database");
    }

    if (input.bad())
        throw std::runtime_error("Error reading user database");

    users = std::move(loaded);
    chmod(path.c_str(), 0600);

    if (needs_migration)
        save_locked();
}

bool AuthStore::valid_username(const std::string& username) {
    if (username.size() < 3 || username.size() > 24)
        return false;

    for (unsigned char c : username) {
        if (!std::isalnum(c) && c != 95 && c != 45 && c != 46)
            return false;
    }
    return true;
}

std::string AuthStore::hash_key(const std::string& key) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(key.data()),
           key.size(), digest);

    std::ostringstream output;
    output << std::hex << std::setfill(static_cast<char>(48));

    for (unsigned char byte : digest)
        output << std::setw(2) << static_cast<unsigned int>(byte);

    return output.str();
}

std::string AuthStore::generate_user_id() {
    std::array<unsigned char, 16> bytes{};

    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
        throw std::runtime_error("Secure UserID generation failed");

    std::ostringstream output;
    output << std::hex << std::setfill(static_cast<char>(48));

    for (unsigned char byte : bytes)
        output << std::setw(2) << static_cast<unsigned int>(byte);

    return output.str();
}

void AuthStore::save_locked() const {
    const std::string temporary = path + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);

    if (!output)
        throw std::runtime_error("Cannot write user database");

    for (const auto& entry : users)
        output << entry.first << " " << entry.second.user_id
               << " " << entry.second.ckey_hash << "\n";

    output.flush();
    if (!output)
        throw std::runtime_error("User database write failed");

    output.close();
    chmod(temporary.c_str(), 0600);

    if (std::rename(temporary.c_str(), path.c_str()) != 0)
        throw std::runtime_error("Cannot replace user database");

    chmod(path.c_str(), 0600);
}

bool AuthStore::register_user(
    const std::string& username, std::string& ckey
) {
    std::string user_id;
    return register_user(username, ckey, user_id);
}

bool AuthStore::register_user(
    const std::string& username, std::string& ckey,
    std::string& user_id
) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!valid_username(username) || users.count(username))
        return false;

    std::array<unsigned char, 32> random_bytes{};

    if (RAND_bytes(random_bytes.data(),
                   static_cast<int>(random_bytes.size())) != 1)
        throw std::runtime_error("Secure CKey generation failed");

    std::ostringstream token;
    token << std::hex << std::setfill(static_cast<char>(48));

    for (unsigned char byte : random_bytes)
        token << std::setw(2) << static_cast<unsigned int>(byte);

    ckey = token.str();
    user_id = generate_user_id();

    users[username] = {user_id, hash_key(ckey)};

    try {
        save_locked();
    } catch (...) {
        users.erase(username);
        ckey.clear();
        user_id.clear();
        throw;
    }

    return true;
}

bool AuthStore::authenticate(
    const std::string& username, const std::string& ckey
) const {
    std::string user_id;
    return authenticate(username, ckey, user_id);
}

bool AuthStore::authenticate(
    const std::string& username, const std::string& ckey,
    std::string& user_id
) const {
    std::lock_guard<std::mutex> lock(mutex);

    auto found = users.find(username);
    if (found == users.end() || ckey.size() != 64)
        return false;

    std::string supplied = hash_key(ckey);

    if (CRYPTO_memcmp(found->second.ckey_hash.data(),
                      supplied.data(), supplied.size()) != 0)
        return false;

    user_id = found->second.user_id;
    return true;
}
