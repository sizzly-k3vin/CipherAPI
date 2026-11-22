#include "auth.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

int main() {
    const std::string valid_file = "cipher_auth_test_valid.db";
    const std::string malformed_file = "cipher_auth_test_malformed.db";

    std::remove(valid_file.c_str());
    std::remove(malformed_file.c_str());

    try {
        std::string key, id;

        {
            AuthStore auth(valid_file);
            if (!auth.register_user("test_user", key, id)) return 1;

            std::string verified_id;
            if (!auth.authenticate("test_user", key, verified_id)) return 2;
            if (id != verified_id) return 3;
        }

        {
            AuthStore restarted(valid_file);
            std::string after_restart;
            if (!restarted.authenticate("test_user", key, after_restart)) return 4;
            if (after_restart != id) return 5;
        }

        std::cout << "Registration: PASS\n";
        std::cout << "Authentication: PASS\n";
        std::cout << "UserID consistency: PASS\n";
        std::cout << "Restart persistence: PASS\n";

        const std::string malformed_data = "test_user invalid_record\n";
        {
            std::ofstream output(malformed_file);
            output << malformed_data;
        }

        bool rejected = false;
        try {
            AuthStore malformed(malformed_file);
        } catch (const std::runtime_error&) {
            rejected = true;
        }

        if (!rejected) return 6;

        std::ifstream input(malformed_file);
        std::string preserved{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };

        if (preserved != malformed_data) return 7;

        std::cout << "Malformed database rejection: PASS\n";
        std::cout << "Original database preservation: PASS\n";

    } catch (const std::exception& e) {
        std::cerr << "Test error: " << e.what() << '\n';
        return 8;
    }

    std::remove(valid_file.c_str());
    std::remove(malformed_file.c_str());
    return 0;
}
