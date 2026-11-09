#include "library.hpp"
#include "logger.hpp"
#include <iostream>
#include <exception>

int main() {
    try {
        LibraryManager manager("libraries");

        const std::string original = "AAAaaa ABC CipherAPI 123";
        const std::string encoded = manager.encode(original);
        const std::string decoded = manager.decode(encoded);

        std::cout << "\nOriginal: " << original << '\n';
        std::cout << "Encoded length: " << encoded.size() << '\n';
        std::cout << "Decoded: " << decoded << '\n';

        if (decoded != original) {
            CipherLog::error("ROUND-TRIP TEST FAILED");
            return 1;
        }

        CipherLog::info("ROUND-TRIP TEST PASSED");
        return 0;

    } catch (const std::exception& e) {
        CipherLog::error(e.what());
        return 1;
    }
}
