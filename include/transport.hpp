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
    TEXT  = 1,
    IMAGE = 2
};

constexpr uint32_t MAX_TEXT_FRAME  = 1024 * 1024;
constexpr uint32_t MAX_IMAGE_FRAME = 32 * 1024 * 1024;

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
    const uint32_t limit =
        type == IMAGE ? MAX_IMAGE_FRAME : MAX_TEXT_FRAME;

    if (payload.size() > limit)
        throw std::runtime_error("Frame exceeds size limit");

    if (type == TEXT && payload.size() % 64 != 0)
        throw std::runtime_error("Invalid encoded text payload");

    uint8_t packet_type = static_cast<uint8_t>(type);
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

    uint32_t length = ntohl(network_length);

    if (type != TEXT && type != IMAGE)
        throw std::runtime_error("Unknown packet type");

    const uint32_t limit =
        type == IMAGE ? MAX_IMAGE_FRAME : MAX_TEXT_FRAME;

    if (length > limit)
        throw std::runtime_error("Incoming frame exceeds size limit");

    if (type == TEXT && length % 64 != 0)
        throw std::runtime_error("Invalid encoded text frame");

    payload.resize(length);

    if (length && !receive_exact(fd, payload.data(), length))
        throw std::runtime_error("Incomplete frame payload");

    return true;
}

}
