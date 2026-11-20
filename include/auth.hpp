#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

class AuthStore {
public:
    explicit AuthStore(const std::string& path = "users.db");

    bool register_user(const std::string& username, std::string& ckey);
    bool register_user(const std::string& username, std::string& ckey,
                       std::string& user_id);

    bool authenticate(const std::string& username,
                      const std::string& ckey) const;
    bool authenticate(const std::string& username,
                      const std::string& ckey,
                      std::string& user_id) const;

private:
    struct UserRecord {
        std::string user_id;
        std::string ckey_hash;
    };

    std::string path;
    mutable std::mutex mutex;
    std::unordered_map<std::string, UserRecord> users;

    static bool valid_username(const std::string& username);
    static std::string hash_key(const std::string& key);
    static std::string generate_user_id();
    void save_locked() const;
};
