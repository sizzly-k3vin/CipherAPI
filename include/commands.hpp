#pragma once

#include <string>
#include <vector>

namespace CipherCommands {

struct Command {
    std::string name;
    std::vector<std::string> args;
};

Command parse(const std::string& line);

}
