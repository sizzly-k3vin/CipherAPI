#pragma once
#include <cstdint>
#include <unordered_map>
class Profile {public:void generate();uint32_t encode(char c)const;bool validate(char c,uint32_t token)const;private:std::unordered_map<char,uint32_t> mapping;};
