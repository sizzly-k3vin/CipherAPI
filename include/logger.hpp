#pragma once
#include <chrono>
#include <cerrno>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

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


inline const char* errno_name(int code) {
    if (code == EADDRINUSE) return "EADDRINUSE";
    if (code == EADDRNOTAVAIL) return "EADDRNOTAVAIL";
    if (code == EACCES) return "EACCES";
    if (code == EBADF) return "EBADF";
    if (code == EINTR) return "EINTR";
    if (code == EINVAL) return "EINVAL";
    if (code == EMFILE) return "EMFILE";
    if (code == ENFILE) return "ENFILE";
    if (code == ENOMEM) return "ENOMEM";
    if (code == ENOBUFS) return "ENOBUFS";
    if (code == ECONNREFUSED) return "ECONNREFUSED";
    if (code == ECONNRESET) return "ECONNRESET";
    if (code == ETIMEDOUT) return "ETIMEDOUT";
    if (code == EPIPE) return "EPIPE";
    if (code == ENOTCONN) return "ENOTCONN";
    return "UNKNOWN";
}

inline void system_error(const std::string& id,
                         const std::string& operation,
                         int code) {
    const std::string detail =
        std::error_code(code, std::generic_category()).message();

    write("ERROR",
          "[" + id + "] " + operation +
          " | errno=" + std::to_string(code) +
          " | name=" + errno_name(code) +
          " | detail=" + detail,
          std::cerr);
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
