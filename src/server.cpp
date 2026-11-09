#include "library.hpp"
#include "logger.hpp"
#include "transport.hpp"
#include "neon.hpp"

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

    CipherTransport::send_frame(fd, encoded);

    ++stats.sent_messages;
    stats.sent_chars += message.size();
    stats.sent_tokens += encoded.size() / 64;
    stats.sent_bytes += encoded.size();

    Neon::send("Frame transmitted successfully");
}

int main() {
    int server = -1;

    try {
        Neon::banner("SERVER");
        LibraryManager libraries("libraries");
        Neon::info("Translation engine initialized");

        server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0) throw std::runtime_error("Socket creation failed");

        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(PORT);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(server, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) < 0)
            throw std::runtime_error("Bind failed");

        if (listen(server, 5) < 0)
            throw std::runtime_error("Listen failed");

        Neon::network("Listening on 127.0.0.1:8080");

        while (true) {
            int client = accept(server, nullptr, nullptr);

            if (client < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("Accept failed");
            }

            Neon::network("Client connected");
            Neon::info("Full-duplex session started");
            Neon::show_help_hint();

            Neon::Stats stats;
            std::atomic<bool> connected{true};

            std::thread receiver([&]() {
                try {
                    std::string encoded;

                    while (connected &&
                           CipherTransport::receive_frame(client, encoded)) {
                        stats.received_bytes += encoded.size();
                        stats.received_tokens += encoded.size() / 64;

                        Neon::receive(
                            std::to_string(encoded.size()) + " bytes | " +
                            std::to_string(encoded.size() / 64) + " tokens"
                        );

                        const auto start = std::chrono::steady_clock::now();
                        std::string message = libraries.decode(encoded);
                        const auto end = std::chrono::steady_clock::now();

                        const double ms =
                            std::chrono::duration<double, std::milli>(
                                end - start).count();

                        ++stats.received_messages;
                        stats.received_chars += message.size();

                        Neon::decode(
                            std::to_string(encoded.size() / 64) +
                            " tokens -> " + std::to_string(message.size()) +
                            " chars | " + std::to_string(ms) + " ms"
                        );

                        Neon::print_message("CLIENT", message);

                        if (message == "/quit") {
                            connected = false;
                            shutdown(client, SHUT_RDWR);
                            break;
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

                if (!connected) break;

                transmit(client, libraries, stats, message);

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
