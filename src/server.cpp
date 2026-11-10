#include "debug.hpp"
#include "library.hpp"
#include "image.hpp"
#include "logger.hpp"
#include "transport.hpp"
#include "neon.hpp"
#include "video.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

constexpr uint16_t PORT = 8080;

static void transmit(int fd, LibraryManager& libraries,
                     Neon::Stats& stats, const std::string& message) {
    const auto start = std::chrono::steady_clock::now();
    std::string encoded = libraries.encode(message);
    const auto end = std::chrono::steady_clock::now();

    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    Neon::encode(
        std::to_string(message.size()) + " chars | " +
        std::to_string(encoded.size() / 64) + " tokens | " +
        std::to_string(encoded.size()) + " bytes | " +
        std::to_string(ms) + " ms"
    );

    CipherTransport::send_frame(fd, CipherTransport::TEXT, encoded);

    ++stats.sent_messages;
    stats.sent_chars += message.size();
    stats.sent_tokens += encoded.size() / 64;
    stats.sent_bytes += encoded.size();

    Neon::send("Text frame transmitted");
}

static void transmit_image(int fd, Neon::Stats& stats,
                           const std::string& path) {
    const auto start = std::chrono::steady_clock::now();

    std::string packet = ImageLibrary::pack(path);

    const auto packed = std::chrono::steady_clock::now();

    CipherTransport::send_frame(fd, CipherTransport::IMAGE, packet);

    const auto sent = std::chrono::steady_clock::now();

    const double pack_ms =
        std::chrono::duration<double, std::milli>(packed - start).count();

    const double send_ms =
        std::chrono::duration<double, std::milli>(sent - packed).count();

    ++stats.sent_messages;
    stats.sent_bytes += packet.size();

    Neon::info(
        "Image packet | " + std::to_string(packet.size()) +
        " bytes | pack " + std::to_string(pack_ms) +
        " ms | send " + std::to_string(send_ms) + " ms"
    );

    Neon::send("Image transmitted successfully");
}

static void transmit_fast(int fd, Neon::Stats& stats,
                           const std::string& message) {
    CipherTransport::send_frame(fd, CipherTransport::FAST_TEXT, message);
    ++stats.sent_messages;
    stats.sent_chars += message.size();
    stats.sent_bytes += message.size();
    Neon::send("Raw text frame transmitted");
}

static void transmit_raw_image(int fd, Neon::Stats& stats,
                               const std::string& path) {
    std::string packet = ImageLibrary::pack_raw(path);
    CipherTransport::send_frame(fd, CipherTransport::RAW_IMAGE, packet);
    ++stats.sent_messages;
    stats.sent_bytes += packet.size();
    Neon::send("Raw image transmitted (no SHA-256)");
}


static void transmit_video(int fd, const LibraryManager& libraries,
                           Neon::Stats& stats, const std::string& path,
                           bool use_sha256) {
    const auto start = std::chrono::steady_clock::now();

    auto result = CipherVideo::send_file(
        fd, libraries, path, use_sha256
    );

    const auto end = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(end - start).count();

    ++stats.sent_messages;
    stats.sent_bytes += result.wire_bytes;

    Neon::info(
        "CVS/1 | " + std::to_string(result.original_bytes) +
        " original bytes | " + std::to_string(result.wire_bytes) +
        " wire bytes | " + std::to_string(result.chunks) +
        " chunks | " + std::to_string(seconds) + " seconds"
    );

    Neon::send(
        use_sha256
            ? "Video transmitted with SHA-256"
            : "Video transmitted without SHA-256"
    );
}

int main() {
    int server = -1;

    try {
        Neon::banner("SERVER");

        LibraryManager libraries("libraries");
        Neon::info("Translation engine initialized");
        Neon::info("Binary image transport initialized");

        server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0) {
            const int err = errno;
            CipherLog::system_error("NET-001", "socket()", err);
            throw std::runtime_error("Server socket initialization failed");
        }

        Neon::network("TCP socket created");

        int reuse = 1;
        if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
                       &reuse, sizeof(reuse)) < 0) {
            const int err = errno;
            CipherLog::system_error("NET-008", "setsockopt()", err);
            throw std::runtime_error("Socket configuration failed");
        }

        Neon::network("SO_REUSEADDR enabled");

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(PORT);
        address.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(server, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) < 0) {
            const int err = errno;
            CipherLog::system_error("NET-002", "bind(127.0.0.1:8080)", err);
            throw std::runtime_error("Server bind failed");
        }

        Neon::network("Socket bound to 0.0.0.0:8080");

        if (listen(server, 5) < 0) {
            const int err = errno;
            CipherLog::system_error("NET-003", "listen()", err);
            throw std::runtime_error("Server listen failed");
        }

        Neon::network("Listening socket ready");

        Neon::network("Listening on 0.0.0.0:8080");

        while (true) {
            int client = accept(server, nullptr, nullptr);

            if (client < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("Accept failed");
            }

            Neon::network("Client connected");
            Neon::info("Full-duplex text + binary session started");
            Neon::show_help_hint();

            Neon::Stats stats;
            bool fast_mode = false;
            std::atomic<bool> connected{true};

            std::thread receiver([&]() {
                CipherVideo::Receiver video_receiver(libraries);
                try {
                    uint8_t type = 0;
                    std::string payload;

                    while (connected &&
                           CipherTransport::receive_frame(
                               client, type, payload)) {
                        stats.received_bytes += payload.size();

                        if (type == CipherTransport::TEXT) {
                            stats.received_tokens += payload.size() / 64;

                            Neon::receive(
                                std::to_string(payload.size()) +
                                " bytes | " +
                                std::to_string(payload.size() / 64) +
                                " tokens"
                            );

                            const auto start =
                                std::chrono::steady_clock::now();

                            std::string message =
                                libraries.decode(payload);

                            const auto end =
                                std::chrono::steady_clock::now();

                            const double ms =
                                std::chrono::duration<double, std::milli>(
                                    end - start).count();

                            ++stats.received_messages;
                            stats.received_chars += message.size();

                            Neon::decode(
                                std::to_string(payload.size() / 64) +
                                " tokens -> " +
                                std::to_string(message.size()) +
                                " chars | " + std::to_string(ms) + " ms"
                            );

                            Neon::print_message("CLIENT", message);

                            if (message == "/quit") {
                                connected = false;
                                shutdown(client, SHUT_RDWR);
                                break;
                            }
                        }
                        else if (type == CipherTransport::FAST_TEXT) {

                            ++stats.received_messages;

                            stats.received_chars += payload.size();

                            Neon::receive("Raw text | " + std::to_string(payload.size()) + " bytes");

                            Neon::print_message("CLIENT", payload);

                            if (payload == "/quit") {

                                connected = false;

                                shutdown(client, SHUT_RDWR);

                                break;

                            }

                        }

                        else if (type == CipherTransport::RAW_IMAGE) {

                            Neon::receive("Raw image packet | " + std::to_string(payload.size()) + " bytes");

                            std::string saved = ImageLibrary::unpack_raw(payload);

                            ++stats.received_messages;

                            Neon::print_message("IMAGE", "Saved to " + saved + " (no checksum)");

                        }

                        else if (type == CipherTransport::VIDEO_START ||
                                 type == CipherTransport::VIDEO_DATA ||
                                 type == CipherTransport::VIDEO_END) {
                            if (type == CipherTransport::VIDEO_START)
                                Neon::receive("CVS/1 video transfer started");

                            std::string saved = video_receiver.handle(type, payload);

                            if (!saved.empty()) {
                                ++stats.received_messages;
                                Neon::decode("CVS/1 video reconstructed");
                                Neon::print_message("VIDEO", "Saved to " + saved);
                            }
                        }
                        else if (type == CipherTransport::IMAGE) {
                            Neon::receive(
                                "Binary image packet | " +
                                std::to_string(payload.size()) + " bytes"
                            );

                            const auto start =
                                std::chrono::steady_clock::now();

                            std::string saved =
                                ImageLibrary::unpack(payload);

                            const auto end =
                                std::chrono::steady_clock::now();

                            const double ms =
                                std::chrono::duration<double, std::milli>(
                                    end - start).count();

                            ++stats.received_messages;

                            Neon::decode(
                                "SHA-256 verified | " +
                                std::to_string(payload.size()) +
                                " bytes | " + std::to_string(ms) + " ms"
                            );

                            Neon::print_message("IMAGE", "Saved to " + saved);
                        }
                    }
                } catch (const std::exception& e) {
                    if (connected) Neon::error(e.what());
                }

                connected = false;
                shutdown(client, SHUT_RDWR);
                Neon::network("Client receive channel closed");
            });

            std::string message;

            while (connected) {
                Neon::prompt("SERVER");

                if (!std::getline(std::cin, message)) {
                    connected = false;
                    shutdown(client, SHUT_RDWR);
                    break;
                }

                if (message == "/help") {
                    Neon::help();
                    continue;
                }

                if (message == "/stats") {
                    Neon::show_stats(stats);
                    continue;
                }

                if (message == "/clear") {
                    Neon::clear();
                    continue;
                }

                if (message == "/fast") {


                    fast_mode = !fast_mode;


                    Neon::info(std::string("Raw text mode ") +


                               (fast_mode ? "ENABLED" : "DISABLED"));


                    continue;


                }



                if (message.rfind("/sendvid ", 0) == 0) {
                    std::string argument = message.substr(9);
                    bool no_sha = argument.rfind("-f ", 0) == 0;
                    std::string path = no_sha ? argument.substr(3) : argument;

                    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
                        path = path.substr(1, path.size() - 2);

                    try {
                        transmit_video(client, libraries, stats, path, !no_sha);
                    } catch (const std::exception& e) {
                        Neon::error(e.what());
                    }
                    continue;
                }

                if (message.rfind("/sendimg ", 0) == 0) {
                    bool raw = message.rfind("/sendimg -f ", 0) == 0;
                    std::string path = raw ? message.substr(12) : message.substr(9);

                    if (path.size() >= 2 &&
                        path.front() == '"' && path.back() == '"') {
                        path = path.substr(1, path.size() - 2);
                    }

                    try {
                        if (raw)
                            transmit_raw_image(client, stats, path);
                        else
                            transmit_image(client, stats, path);
                    } catch (const std::exception& e) {
                        Neon::error(e.what());
                    }
                    continue;
                }

                if (!connected) break;

                try {
                    if (fast_mode)
                        transmit_fast(client, stats, message);
                    else
                        transmit(client, libraries, stats, message);
                } catch (const std::exception& e) {
                    Neon::error(e.what());
                    connected = false;
                    shutdown(client, SHUT_RDWR);
                    break;
                }

                if (message == "/quit") {
                    connected = false;
                    shutdown(client, SHUT_RDWR);
                    break;
                }
            }

            if (receiver.joinable()) receiver.join();

            close(client);

            Neon::network("Client session ended");
            Neon::show_stats(stats);
        }

        close(server);
        server = -1;

    } catch (const std::exception& e) {
        if (server >= 0) close(server);

        Neon::error(e.what());
        return 1;
    }

    return 0;
}
