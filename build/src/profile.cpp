#include "profile.hpp"
#include <random>
#include <stdexcept>

void Profile::generate() {
    forward.clear();
    reverse.clear();
    std::random_device rd;
    std::mt19937 rng(rd());

    for (unsigned i=0;i<256;++i) {
        uint32_t token;
        do { token=rng(); }
        while(reverse.count(token));

        forward[static_cast<unsigned char>(i)]=token;
        reverse[token]=static_cast<unsigned char>(i);
    }
}

std::vector<uint32_t> Profile::encode(const std::string& text) const {
    std::vector<uint32_t> out;
    for(unsigned char c:text) {
        auto it=forward.find(c);
        if(it==forward.end()) throw std::runtime_error("Profile not initialized");
        out.push_back(it->second);
    }
    return out;
}

std::string Profile::decode(const std::vector<uint32_t>& tokens) const {
    std::string out;
    for(uint32_t t:tokens) {
        auto it=reverse.find(t);
        if(it==reverse.end()) throw std::runtime_error("Unknown token");
        out.push_back(static_cast<char>(it->second));
    }
    return out;
}
