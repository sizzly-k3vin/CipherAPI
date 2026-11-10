#pragma once
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <source_location>
#include <string_view>

namespace CipherDebug {
inline std::mutex mutex;

inline bool enabled() {
    const char* v = std::getenv("CIPHER_DEBUG");
    return v && std::string_view(v) == "1";
}

inline void log(std::string_view level, std::string_view msg,
                const std::source_location loc =
                    std::source_location::current()) {

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::lock_guard lock(mutex);
    std::cerr << "[" << level << "] "
              << std::chrono::system_clock::to_time_t(now)
              << "." << ms.count()
              << " [T:" << std::this_thread::get_id() << "] "
              << loc.file_name() << ":" << loc.line()
              << " | " << loc.function_name()
              << " | " << msg << '\n';
}
}

#define CDEBUG(msg) CipherDebug::log("DEBUG", msg)
#define CINFO(msg) CipherDebug::log("INFO", msg)
#define CWARN(msg) CipherDebug::log("WARN", msg)
#define CERROR(msg) CipherDebug::log("ERROR", msg)
