#include "debug.hpp"
#include "library.hpp"
#include "image.hpp"
#include "logger.hpp"
#include "transport.hpp"
#include "neon.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
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

static void transmit_fast(int fd, Neon::Stats& stats,
                          const std::string& message) {
    CipherTransport::send_frame(fd, CipherTransport::FAST_TEXT, message);

    ++stats.sent_messages;
    stats.sent_chars += message.size();
    stats.sent_bytes += message.size();

    Neon::info("FAST text | " + std::to_string(message.size()) + " raw bytes");
    Neon::send("Binary text frame transmitted");
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

static void transmit_raw_image(int fd, Neon::Stats& stats,
                               const std::string& path) {
    const auto start = std::chrono::steady_clock::now();

    std::string packet = ImageLibrary::pack_raw(path);
    const auto packed = std::chrono::steady_clock::now();

    CipherTransport::send_frame(fd, CipherTransport::RAW_IMAGE, packet);
    const auto sent = std::chrono::steady_clock::now();

    double pack_ms =
        std::chrono::duration<double, std::milli>(packed - start).count();
    double send_ms =
        std::chrono::duration<double, std::milli>(sent - packed).count();

    ++stats.sent_messages;
    stats.sent_bytes += packet.size();

    Neon::info("RAW image | " + std::to_string(packet.size()) +
               " bytes | pack " + std::to_string(pack_ms) +
               " ms | send " + std::to_string(send_ms) + " ms");
    Neon::send("Raw image transmitted (no SHA-256)");
}

int main(int argc, char* argv[]) {
    int sock = -1;

    try {
        Neon::banner("CLIENT");

        LibraryManager libraries("libraries");
        Neon::info("Translation engine initialized");
        Neon::info("Binary image transport initialized");

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
            throw std::runtime_error("Socket creation failed");

        sockaddr_in server{};
        server.sin_family = AF_INET;
        server.sin_port = htons(PORT);
        const char* server_ip = argc > 1 ? argv[1] : "127.0.0.1";
        if (inet_pton(AF_INET, server_ip, &server.sin_addr) != 1)
            throw std::runtime_error("Invalid IPv4 address");

        Neon::network(std::string("Connecting to ") +
                     server_ip + ":8080");

        if (connect(sock, reinterpret_cast<sockaddr*>(&server),
                    sizeof(server)) < 0)
            throw std::runtime_error("Connection failed");

        Neon::network("Connection established");
        Neon::info("Full-duplex text + binary communication enabled");
        Neon::show_help_hint();

        Neon::Stats stats;
            bool fast_mode = false;
        std::atomic<bool> connected{true};

        std::thread receiver([&]() {
            try {
                uint8_t type = 0;
                std::string payload;

                while (connected &&
                       CipherTransport::receive_frame(sock, type, payload)) {
                    stats.received_bytes += payload.size();

                    if (type == CipherTransport::TEXT) {
                        stats.received_tokens += payload.size() / 64;

                        Neon::receive(
                            std::to_string(payload.size()) + " bytes | " +
                            std::to_string(payload.size() / 64) + " tokens"
                        );

                        const auto start = std::chrono::steady_clock::now();
                        std::string message = libraries.decode(payload);
                        const auto end = std::chrono::steady_clock::now();

                        const double ms =
                            std::chrono::duration<double, std::milli>(
                                end - start).count();

                        ++stats.received_messages;
                        stats.received_chars += message.size();

                        Neon::decode(
                            std::to_string(payload.size() / 64) +
                            " tokens -> " + std::to_string(message.size()) +
                            " chars | " + std::to_string(ms) + " ms"
                        );

                        Neon::print_message("SERVER", message);

                        if (message == "/quit") {
                            connected = false;
                            shutdown(sock, SHUT_RDWR);
                            break;
                        }
                    }
                    else if (type == CipherTransport::FAST_TEXT) {
                            ++stats.received_messages;
                            stats.received_chars += payload.size();
                            Neon::receive("FAST text | " +
                                          std::to_string(payload.size()) +
                                          " raw bytes");
                            Neon::print_message("PEER", payload);

                            if (payload == "/quit") {
                                connected = false;
                                shutdown(sock, SHUT_RDWR);
                                break;
                            }
                        }
                        else if (type == CipherTransport::RAW_IMAGE) {
                            Neon::receive("Raw image | " +
                                          std::to_string(payload.size()) +
                                          " bytes | no checksum");

                            std::string saved = ImageLibrary::unpack_raw(payload);
                            ++stats.received_messages;

                            Neon::info("Raw image saved without SHA-256 verification");
                            Neon::print_message("IMAGE", "Saved to " + saved);
                        }
                        else if (type == CipherTransport::IMAGE) {
                        Neon::receive(
                            "Binary image packet | " +
                            std::to_string(payload.size()) + " bytes"
                        );

                        const auto start = std::chrono::steady_clock::now();
                        std::string saved =
                            ImageLibrary::unpack(payload);
                        const auto end = std::chrono::steady_clock::now();

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
            shutdown(sock, SHUT_RDWR);
            Neon::network("Receive channel closed");
        });

        std::string message;

        while (connected) {
            Neon::prompt("CLIENT");

            if (!std::getline(std::cin, message)) {
                if (connected) {
                    try { transmit(sock, libraries, stats, "/quit"); }
                    catch (...) {}
                }
                connected = false;
                shutdown(sock, SHUT_RDWR);
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
                Neon::info(std::string("FAST binary text mode ") +
                           (fast_mode ? "ENABLED" : "DISABLED"));
                continue;
            }

            if (message.rfind("/sendimg ", 0) == 0) {
                std::string argument = message.substr(9);
                bool raw_mode = argument.rfind("-f ", 0) == 0;
                std::string path = raw_mode ? argument.substr(3) : argument;

                if (path.size() >= 2 &&
                    path.front() == '"' && path.back() == '"') {
                    path = path.substr(1, path.size() - 2);
                }

                try {
                    if (raw_mode)
                        transmit_raw_image(sock, stats, path);
                    else
                        transmit_image(sock, stats, path);
                } catch (const std::exception& e) {
                    Neon::error(e.what());
                }
                continue;
            }

            if (!connected) break;

            try {
                if (fast_mode)
                    transmit_fast(sock, stats, message);
                else
                    transmit(sock, libraries, stats, message);
            } catch (const std::exception& e) {
                Neon::error(e.what());
                connected = false;
                shutdown(sock, SHUT_RDWR);
                break;
            }

            if (message == "/quit") {
                connected = false;
                shutdown(sock, SHUT_RDWR);
                break;
            }
        }

        if (receiver.joinable()) receiver.join();

        close(sock);
        sock = -1;

        Neon::network("Client session closed");
        Neon::show_stats(stats);

    } catch (const std::exception& e) {
        if (sock >= 0) {
            shutdown(sock, SHUT_RDWR);
            close(sock);
        }

        Neon::error(e.what());
        return 1;
    }

    return 0;
}
