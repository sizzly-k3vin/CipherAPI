#pragma once
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace CipherLog {
inline std::mutex log_mutex;

inline void write(const char* level, const std::string& message,
                  std::ostream& output) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);

    std::lock_guard<std::mutex> lock(log_mutex);
    output << '[' << std::put_time(&local, "%H:%M:%S")
           << "] [" << level << "] " << message << '\n';
    output.flush();
}

inline void info(const std::string& s)    { write("INFO", s, std::cout); }
inline void error(const std::string& s)   { write("FAIL", s, std::cerr); }
inline void network(const std::string& s) { write("NET ", s, std::cout); }
inline void encode(const std::string& s)  { write("ENC ", s, std::cout); }
inline void decode(const std::string& s)  { write("DEC ", s, std::cout); }
inline void send(const std::string& s)    { write("SEND", s, std::cout); }
inline void receive(const std::string& s) { write("RECV", s, std::cout); }

inline void banner(const std::string& role) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout
        << "\n"
        << "  ========================================\n"
        << "          C I P H E R A P I\n"
        << "             VERSION 0.5\n"
        << "  ========================================\n"
        << "  MODE: " << role << '\n'
        << "  TRANSPORT: TCP / FULL DUPLEX\n"
        << "  ENCODING: EXPERIMENTAL TOKEN MAPPING\n"
        << "  ========================================\n\n";
}
}
