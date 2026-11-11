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

static int wait_readable(int fd, int timeout_ms) {
    pollfd descriptor{fd, POLLIN, 0};
    int result;

    do {
        result = poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);

    if (result < 0)
        throw std::runtime_error("Socket poll failed");

    return result;
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
            client->fd, *client->libraries, path, checksum != 0
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

        while (true) {
            int ready = wait_readable(client->fd, timeout_ms);

            if (ready == 0)
                return -2;

            uint8_t type = 0;
            std::string payload;

            if (!CipherTransport::receive_frame(client->fd, type, payload))
                throw std::runtime_error("Server closed the connection");

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
