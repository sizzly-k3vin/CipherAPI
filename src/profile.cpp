#include "profile.hpp"
#include <random>
#include <stdexcept>
void Profile::generate(){std::mt19937 rng(std::random_device{}());for(char c=32;c<127;++c)mapping[c]=rng();}
uint32_t Profile::encode(char c)const{auto i=mapping.find(c);if(i==mapping.end())throw std::runtime_error("Invalid character");return i->second;}
bool Profile::validate(char c,uint32_t t)const{auto i=mapping.find(c);return i!=mapping.end()&&i->second==t;}
