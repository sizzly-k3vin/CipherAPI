#include "auth.hpp"
#include <iostream>
#include <string>

int main() {
    AuthStore auth("cipher_test_users.db");
    std::string key, id;

    if (!auth.register_user("test_user", key, id)) return 1;

    std::string verified_id;
    if (!auth.authenticate("test_user", key, verified_id)) return 2;
    if (id != verified_id) return 3;
    {
        AuthStore restarted("cipher_test_users.db");
        std::string after_restart;
        if (!restarted.authenticate("test_user", key, after_restart)) return 4;
        if (after_restart != id) return 5;
    }

    std::cout << "Registration: PASS\\n";
    std::cout << "Authentication: PASS\\n";
    std::cout << "UserID consistency: PASS\\n";
    std::cout << "Restart persistence: PASS\\n";
}
