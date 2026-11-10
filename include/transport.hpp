#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace CipherTransport {

enum FrameType : uint8_t {
    TEXT = 1,
    IMAGE = 2,
    FAST_TEXT = 3,
    RAW_IMAGE = 4
};

constexpr uint32_t MAX_TEXT_FRAME = 1024 * 1024;
constexpr uint32_t MAX_IMAGE_FRAME = 32 * 1024 * 1024;

inline bool valid_type(uint8_t type) {
    return type == TEXT || type == IMAGE ||
           type == FAST_TEXT || type == RAW_IMAGE;
}

inline uint32_t frame_limit(uint8_t type) {
    return (type == IMAGE || type == RAW_IMAGE)
        ? MAX_IMAGE_FRAME : MAX_TEXT_FRAME;
}

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

inline void send_frame(int fd, FrameType type,
                       const std::string& payload) {
    uint8_t packet_type = static_cast<uint8_t>(type);

    if (!valid_type(packet_type))
        throw std::runtime_error("Unknown outgoing frame type");

    if (payload.size() > frame_limit(packet_type))
        throw std::runtime_error("Frame exceeds size limit");

    if (type == TEXT && payload.size() % 64 != 0)
        throw std::runtime_error("Invalid encoded text payload");

    uint32_t length = htonl(static_cast<uint32_t>(payload.size()));

    send_exact(fd, &packet_type, sizeof(packet_type));
    send_exact(fd, &length, sizeof(length));

    if (!payload.empty())
        send_exact(fd, payload.data(), payload.size());
}

inline bool receive_frame(int fd, uint8_t& type,
                          std::string& payload) {
    uint32_t network_length = 0;

    if (!receive_exact(fd, &type, sizeof(type)))
        return false;

    if (!receive_exact(fd, &network_length, sizeof(network_length)))
        throw std::runtime_error("Incomplete frame header");

    if (!valid_type(type))
        throw std::runtime_error("Unknown packet type");

    uint32_t length = ntohl(network_length);

    if (length > frame_limit(type))
        throw std::runtime_error("Incoming frame exceeds size limit");

    if (type == TEXT && length % 64 != 0)
        throw std::runtime_error("Invalid encoded text frame");

    payload.resize(length);

    if (length && !receive_exact(fd, payload.data(), length))
        throw std::runtime_error("Incomplete frame payload");

    return true;
}

}
