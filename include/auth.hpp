#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

class AuthStore {
public:
    explicit AuthStore(const std::string& path = "users.db");

    bool register_user(const std::string& username, std::string& ckey);
    bool authenticate(const std::string& username, const std::string& ckey) const;

private:
    std::string path;
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::string> users;

    static bool valid_username(const std::string& username);
    static std::string hash_key(const std::string& key);
    void save_locked() const;
};
