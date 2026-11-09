#include "profile.hpp"
#include <iostream>
#include <iomanip>

int main() {
    Profile p;
    p.generate();

    std::string message="CipherLink v0.2!";
    auto tokens=p.encode(message);

    std::cout << "Original: " << message << "\\nEncoded: ";
    for(auto t:tokens)
        std::cout << std::hex << std::setw(8) << std::setfill(0) << t << " ";
    std::cout << std::dec << "\\nDecoded: " << p.decode(tokens) << "\\n";
}
