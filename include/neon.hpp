#pragma once
#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace Neon {
inline constexpr const char* RESET  = "\033[0m";
inline constexpr const char* CYAN   = "\033[1;36m";
inline constexpr const char* BLUE   = "\033[1;34m";
inline constexpr const char* GREEN  = "\033[1;32m";
inline constexpr const char* YELLOW = "\033[1;33m";
inline constexpr const char* RED    = "\033[1;31m";
inline constexpr const char* DIM    = "\033[2m";
inline std::mutex output_mutex;

inline std::string sanitize_terminal_text(const std::string& input) {
    std::string output;
    output.reserve(input.size());

    for (unsigned char ch : input) {
        if (ch == 10 || ch == 9) {
            output.push_back(static_cast<char>(ch));
        } else if (ch < 0x20 || ch == 0x7f) {
            output.push_back(static_cast<char>(0x3f));
        } else {
            output.push_back(static_cast<char>(ch));
        }
    }
    return output;
}

struct Stats {
    std::atomic<unsigned long long> sent_messages{0};
    std::atomic<unsigned long long> received_messages{0};
    std::atomic<unsigned long long> sent_chars{0};
    std::atomic<unsigned long long> received_chars{0};
    std::atomic<unsigned long long> sent_tokens{0};
    std::atomic<unsigned long long> received_tokens{0};
    std::atomic<unsigned long long> sent_bytes{0};
    std::atomic<unsigned long long> received_bytes{0};
};

inline std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);
    std::ostringstream out;
    out << std::put_time(&local, "%H:%M:%S");
    return out.str();
}

inline void log(const char* color, const char* tag,
                const std::string& message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << DIM << '[' << timestamp() << "] "
              << RESET << color << '[' << tag << "] "
              << RESET << sanitize_terminal_text(message) << '\n';
    std::cout.flush();
}

inline void info(const std::string& s) {
    log(CYAN, "INFO", s);
}
inline void network(const std::string& s) {
    log(BLUE, "NET ", s);
}
inline void encode(const std::string& s) {
    log(YELLOW, "ENC ", s);
}
inline void decode(const std::string& s) {
    log(GREEN, "DEC ", s);
}
inline void send(const std::string& s) {
    log(CYAN, "SEND", s);
}
inline void receive(const std::string& s) {
    log(BLUE, "RECV", s);
}
inline void error(const std::string& s) {
    log(RED, "ERROR", s);
}

inline void banner(const std::string& role) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout
        << CYAN
        << "\n"
        << "  +------------------------------------------------+\n"
        << "  |                                                |\n"
        << "  |     C I P H E R A P I   //   N E O N           |\n"
        << "  |                                                |\n"
        << "  |              VERSION 0.7                       |\n"
        << "  |                                                |\n"
        << "  +------------------------------------------------+\n"
        << RESET
        << BLUE << "  NODE: " << RESET << role << '\n'
        << BLUE << "  LINK: " << RESET << "TCP / FULL DUPLEX\n"
        << BLUE << "  MODE: " << RESET << "TOKEN + BINARY MULTIMEDIA\n"
        << BLUE << "  STATUS: " << GREEN << "INITIALIZING\n\n"
        << RESET;
    std::cout.flush();
}

inline void prompt(const std::string& role) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << CYAN << '[' << role << "] > " << RESET << std::flush;
}

inline void print_message(const std::string& role,
                          const std::string& message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << '\n' << GREEN << '[' << role << "] "
              << RESET << sanitize_terminal_text(message) << '\n';
    std::cout.flush();
}

inline void clear() {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << "\033[2J\033[H" << std::flush;
}

inline void help() {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << CYAN
        << "\n  CIPHERAPI COMMANDS\n"
        << "  ----------------------------------------\n"
        << RESET
        << "  /help             Show this command list\n"
        << "  /stats            Show session traffic statistics\n"
        << "  /clear            Clear the terminal\n"
        << "  /fast             Toggle fast binary text mode\n"
        << "  /sendimg FILE     Send an image\n"
        << "  /sendimg -f FILE  Send a raw image\n"
        << "  /sendvid FILE     Send video with SHA-256 cycling\n"
        << "  /sendvid -f FILE  Send video without SHA-256 cycling\n"
        << "  /quit             End the current session\n\n";
}

inline void show_stats(const Stats& s) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << CYAN
        << "\n  +------------- SESSION STATISTICS -------------+\n"
        << RESET
        << "  Messages sent:      " << s.sent_messages.load() << '\n'
        << "  Messages received:  " << s.received_messages.load() << '\n'
        << "  Characters sent:    " << s.sent_chars.load() << '\n'
        << "  Characters received:" << s.received_chars.load() << '\n'
        << "  Tokens sent:        " << s.sent_tokens.load() << '\n'
        << "  Tokens received:    " << s.received_tokens.load() << '\n'
        << "  Bytes sent:         " << s.sent_bytes.load() << '\n'
        << "  Bytes received:     " << s.received_bytes.load() << '\n'
        << CYAN
        << "  +----------------------------------------------+\n\n"
        << RESET;
}

inline void show_help_hint() {
    info("Type /help for commands");
}
}
