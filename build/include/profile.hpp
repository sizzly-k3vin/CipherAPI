#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Profile {
public:
    void generate();
    std::vector<uint32_t> encode(const std::string& text) const;
    std::string decode(const std::vector<uint32_t>& tokens) const;
private:
    std::unordered_map<unsigned char,uint32_t> forward;
    std::unordered_map<uint32_t,unsigned char> reverse;
};
