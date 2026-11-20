#include "cipherapi.h"
#include "library.hpp"
#include "transport.hpp"
#include "image.hpp"
#include "video.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

struct CAPIClient {
    std::string host;
    uint16_t port;
    std::unique_ptr<LibraryManager> libraries;
    std::unique_ptr<CipherVideo::Receiver> video_receiver;
    int fd = -1;
    std::mutex send_mutex;
    std::mutex receive_mutex;

    CAPIClient(const char* h, uint16_t p, const char* directory)
        : host(h), port(p),
          libraries(std::make_unique<LibraryManager>(directory)) {
        video_receiver =
            std::make_unique<CipherVideo::Receiver>(*libraries);
    }
};

static thread_local std::string last_error;

static void set_error(const char* message) {
    last_error = message ? message : "Unknown CipherAPI error";
}

class FrameTimeoutError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

static bool wait_readable_until(
    int fd, std::chrono::steady_clock::time_point deadline, bool infinite
) {
    while (true) {
        int timeout = -1;

        if (!infinite) {
            auto remaining = deadline - std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                remaining
            ).count();

            if (remaining > std::chrono::steady_clock::duration::zero() &&
                ms == 0)
                ms = 1;

            if (ms < 0)
                ms = 0;

            timeout = ms > INT_MAX ? INT_MAX : static_cast<int>(ms);
        }

        pollfd descriptor{fd, POLLIN, 0};
        int result = poll(&descriptor, 1, timeout);

        if (result < 0 && errno == EINTR)
            continue;

        if (result < 0)
            throw std::runtime_error("Socket poll failed");

        if (result == 0)
            return false;

        if (descriptor.revents & POLLNVAL)
            throw std::runtime_error("Invalid socket descriptor");

        return true;
    }
}

static bool receive_exact_until(
    int fd, void* data, size_t size,
    std::chrono::steady_clock::time_point deadline,
    bool infinite, bool allow_initial_timeout
) {
    char* ptr = static_cast<char*>(data);
    size_t received = 0;

    while (received < size) {
        if (!wait_readable_until(fd, deadline, infinite)) {
            if (received == 0 && allow_initial_timeout)
                return false;

            throw FrameTimeoutError(
                "Receive timed out during an incomplete frame"
            );
        }

        ssize_t n = recv(
            fd, ptr + received, size - received, MSG_DONTWAIT
        );

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            continue;

        if (n == 0)
            throw std::runtime_error("Server closed the connection");

        if (n < 0)
            throw std::runtime_error("TCP receive failed");

        received += static_cast<size_t>(n);
    }

    return true;
}

static bool receive_frame_until(
    int fd, uint8_t& type, std::string& payload,
    std::chrono::steady_clock::time_point deadline, bool infinite
) {
    uint32_t network_length = 0;

    if (!receive_exact_until(
            fd, &type, sizeof(type), deadline, infinite, true
        ))
        return false;

    receive_exact_until(
        fd, &network_length, sizeof(network_length),
        deadline, infinite, false
    );

    if (!CipherTransport::valid_type(type))
        throw std::runtime_error("Unknown packet type");

    uint32_t length = ntohl(network_length);

    if (length > CipherTransport::frame_limit(type))
        throw std::runtime_error("Incoming frame exceeds size limit");

    if (type == CipherTransport::TEXT && length % 64 != 0)
        throw std::runtime_error("Invalid encoded text frame");

    payload.resize(length);

    if (length) {
        receive_exact_until(
            fd, payload.data(), length, deadline, infinite, false
        );
    }

    return true;
}

extern "C" {

const char* capi_last_error(void) {
    return last_error.c_str();
}

CAPIClient* capi_client_create(
    const char* host, uint16_t port, const char* library_directory
) {
    try {
        if (!host || !*host || !library_directory || !*library_directory)
            throw std::invalid_argument("Host and library directory are required");

        last_error.clear();
        return new CAPIClient(host, port, library_directory);
    } catch (const std::exception& e) {
        set_error(e.what());
        return nullptr;
    } catch (...) {
        set_error("Unknown error creating CipherAPI client");
        return nullptr;
    }
}

int capi_client_connect(CAPIClient* client) {
    try {
        if (!client)
            throw std::invalid_argument("Invalid client handle");

        if (client->fd >= 0)
            return 0;

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* result = nullptr;
        const std::string service = std::to_string(client->port);

        int status = getaddrinfo(
            client->host.c_str(), service.c_str(), &hints, &result
        );

        if (status != 0)
            throw std::runtime_error(gai_strerror(status));

        int connected_fd = -1;

        for (addrinfo* item = result; item; item = item->ai_next) {
            int candidate = socket(
                item->ai_family, item->ai_socktype, item->ai_protocol
            );

            if (candidate < 0)
                continue;

            if (::connect(candidate, item->ai_addr, item->ai_addrlen) == 0) {
                connected_fd = candidate;
                break;
            }

            ::close(candidate);
        }

        freeaddrinfo(result);

        if (connected_fd < 0)
            throw std::runtime_error("Could not connect to CipherAPI server");

        client->fd = connected_fd;
        last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        set_error(e.what());
        return -1;
    } catch (...) {
        set_error("Unknown connection error");
        return -1;
    }
}

int capi_client_send_text(CAPIClient* client, const char* message, int fast) {
    try {
        if (!client || !message)
            throw std::invalid_argument("Invalid client or message");

        if (client->fd < 0)
            throw std::runtime_error("Client is not connected");

        std::lock_guard<std::mutex> lock(client->send_mutex);
        std::string text(message);

        if (fast) {
            CipherTransport::send_frame(
                client->fd, CipherTransport::FAST_TEXT, text
            );
        } else {
            std::string encoded = client->libraries->encode(text);
            CipherTransport::send_frame(
                client->fd, CipherTransport::TEXT, encoded
            );
        }

        last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        set_error(e.what());
        return -1;
    } catch (...) {
        set_error("Unknown text send error");
        return -1;
    }
}

int capi_client_send_image(CAPIClient* client, const char* path, int raw) {
    try {
        if (!client || !path || !*path)
            throw std::invalid_argument("Invalid image path");

        if (client->fd < 0)
            throw std::runtime_error("Client is not connected");

        std::lock_guard<std::mutex> lock(client->send_mutex);

        std::string packet = raw
            ? ImageLibrary::pack_raw(path)
            : ImageLibrary::pack(path);

        auto type = raw
            ? CipherTransport::RAW_IMAGE
            : CipherTransport::IMAGE;

        CipherTransport::send_frame(client->fd, type, packet);

        last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        set_error(e.what());
        return -1;
    } catch (...) {
        set_error("Unknown image send error");
        return -1;
    }
}

int capi_client_send_video(CAPIClient* client, const char* path, int checksum) {
    try {
        if (!client || !path || !*path)
            throw std::invalid_argument("Invalid video path");

        if (client->fd < 0)
            throw std::runtime_error("Client is not connected");

        std::lock_guard<std::mutex> lock(client->send_mutex);

        CipherVideo::send_file(
            client->fd, *client->libraries, path, checksum != 0, false
        );

        last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        set_error(e.what());
        return -1;
    } catch (...) {
        set_error("Unknown video send error");
        return -1;
    }
}

int capi_client_receive_event(
    CAPIClient* client, int* event_type,
    char* buffer, size_t capacity, int timeout_ms
) {
    try {
        if (!client || !event_type || !buffer || capacity == 0)
            throw std::invalid_argument("Invalid receive arguments");

        if (client->fd < 0)
            throw std::runtime_error("Client is not connected");

        std::lock_guard<std::mutex> lock(client->receive_mutex);

        const bool infinite_timeout = timeout_ms < 0;
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);

        while (true) {
            uint8_t type = 0;
            std::string payload;

            if (!receive_frame_until(
                    client->fd, type, payload, deadline, infinite_timeout
                ))
                return -2;

            int kind = 0;
            std::string output;

            if (type == CipherTransport::TEXT) {
                output = client->libraries->decode(payload);
                kind = 1;
            } else if (type == CipherTransport::FAST_TEXT) {
                output = std::move(payload);
                kind = 1;
            } else if (type == CipherTransport::IMAGE) {
                output = ImageLibrary::unpack(
                    payload, "received_images"
                );
                kind = 2;
            } else if (type == CipherTransport::RAW_IMAGE) {
                output = ImageLibrary::unpack_raw(
                    payload, "received_images"
                );
                kind = 2;
            } else if (
                type >= CipherTransport::VIDEO_START &&
                type <= CipherTransport::VIDEO_END
            ) {
                output = client->video_receiver->handle(type, payload);

                if (output.empty())
                    continue;

                kind = 3;
            } else {
                throw std::runtime_error("Unsupported incoming frame type");
            }

            if (output.size() + 1 > capacity)
                throw std::runtime_error("Receive buffer is too small");

            std::memcpy(buffer, output.data(), output.size());
            buffer[output.size()] = 0;
            *event_type = kind;

            last_error.clear();
            return static_cast<int>(output.size());
        }
    } catch (const FrameTimeoutError& e) {
        if (client && client->fd >= 0) {
            shutdown(client->fd, SHUT_RDWR);
            close(client->fd);
            client->fd = -1;
        }
        set_error(e.what());
        return -1;
    } catch (const std::exception& e) {
        set_error(e.what());
        return -1;
    } catch (...) {
        set_error("Unknown receive error");
        return -1;
    }
}

int capi_client_receive(
    CAPIClient* client, char* buffer, size_t capacity, int timeout_ms
) {
    int event_type = 0;

    int result = capi_client_receive_event(
        client, &event_type, buffer, capacity, timeout_ms
    );

    if (result < 0)
        return result;

    if (event_type != 1) {
        set_error("Received media event; use receive_event() instead");
        return -1;
    }

    return result;
}

void capi_client_close(CAPIClient* client) {
    if (!client || client->fd < 0)
        return;

    shutdown(client->fd, SHUT_RDWR);
    ::close(client->fd);
    client->fd = -1;
}

void capi_client_destroy(CAPIClient* client) {
    if (!client)
        return;

    capi_client_close(client);
    delete client;
}

}
