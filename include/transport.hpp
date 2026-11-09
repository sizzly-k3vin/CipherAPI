#pragma once
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace CipherTransport {
constexpr uint32_t MAX_FRAME = 1024 * 1024;

inline bool receive_exact(int fd, void* data, size_t size) {
    char* ptr = static_cast<char*>(data);
    size_t received = 0;

    while (received < size) {
        ssize_t n = recv(fd, ptr + received, size - received, 0);
        if (n < 0 && errno == EINTR) continue;

        if (n == 0 && received == 0) return false;
        if (n <= 0) throw std::runtime_error("Incomplete TCP frame");

        received += static_cast<size_t>(n);
    }
    return true;
}

inline void send_exact(int fd, const void* data, size_t size) {
    const char* ptr = static_cast<const char*>(data);
    size_t sent = 0;

    while (sent < size) {
#ifdef MSG_NOSIGNAL
        ssize_t n = ::send(fd, ptr + sent, size - sent, MSG_NOSIGNAL);
#else
        ssize_t n = ::send(fd, ptr + sent, size - sent, 0);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("TCP send failed");
        sent += static_cast<size_t>(n);
    }
}

inline void send_frame(int fd, const std::string& payload) {
    if (payload.size() > MAX_FRAME || payload.size() % 64 != 0)
        throw std::runtime_error("Invalid encoded payload size");

    uint32_t header = htonl(static_cast<uint32_t>(payload.size()));
    send_exact(fd, &header, sizeof(header));

    if (!payload.empty())
        send_exact(fd, payload.data(), payload.size());
}

inline bool receive_frame(int fd, std::string& payload) {
    uint32_t header = 0;
    if (!receive_exact(fd, &header, sizeof(header))) return false;

    const uint32_t length = ntohl(header);
    if (length > MAX_FRAME || length % 64 != 0)
        throw std::runtime_error("Invalid encoded frame length");

    payload.resize(length);
    if (length && !receive_exact(fd, payload.data(), length))
        throw std::runtime_error("Incomplete frame payload");

    return true;
}
}
