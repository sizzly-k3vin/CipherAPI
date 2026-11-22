#include "commands.hpp"

#include <stdexcept>

namespace CipherCommands {

Command parse(const std::string& line) {
    Command result;
    std::string token;
    bool in_quotes = false;
    bool token_started = false;

    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            token_started = true;
        }
        else if (ch == ' ' || ch == '\t') {
            if (in_quotes) {
                token += ch;
            } else if (token_started) {
                result.args.push_back(token);
                token.clear();
                token_started = false;
            }
        }
        else {
            token += ch;
            token_started = true;
        }
    }

    if (in_quotes)
        throw std::invalid_argument("Unclosed quoted argument");

    if (token_started)
        result.args.push_back(token);

    if (!result.args.empty()) {
        result.name = result.args.front();
        result.args.erase(result.args.begin());
    }

    return result;
}

}
